#
# This file is the default set of rules to compile a Pebble application.
#
# Feel free to customize this to your needs.
#
import os.path

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    ctx.load('pebble_sdk')


def build(ctx):
    compile_typescript(ctx)
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    # Test-only AppMessage hooks (see main.c's handle_test_message and
    # CLAUDE.md's "Test hooks" section) are opt-in via env var so a normal
    # `pebble build` never ships them: `APP_TEST_HOOKS=1 pebble build`.
    test_hooks_enabled = os.environ.get('APP_TEST_HOOKS') == '1'

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        if test_hooks_enabled:
            ctx.env.DEFINES += ['APP_TEST_HOOKS=1']
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app')

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')


def compile_typescript(ctx):
    """Compile the phone-side TypeScript (`src/ts/*.ts`) to CommonJS JS in
    `src/pkjs/` *before* the SDK bundles `src/pkjs/**/*.js`.

    The PKJS source lives in `src/ts/` as modern TypeScript; tsc (pinned to 6.x
    in devDependencies, config in `tsconfig.json`) emits ES5/CommonJS 1:1 into
    `src/pkjs/`, which the Pebble SDK's webpack pass then bundles exactly as it
    would hand-written JS. The generated `src/pkjs/*.js` are gitignored — this
    hook regenerates them on every build, so a clean checkout still bundles
    correctly. A type error aborts the build (tsc has noEmitOnError).
    """
    import subprocess, os
    root = ctx.path.abspath()
    if not os.path.isdir(os.path.join(root, 'src', 'ts')):
        return
    tsc = os.path.join(root, 'node_modules', '.bin', 'tsc')
    cmd = [tsc] if os.path.exists(tsc) else ['npx', 'tsc']
    from waflib import Logs
    Logs.pprint('CYAN', 'Compiling TypeScript (src/ts -> src/pkjs)')
    try:
        subprocess.run(cmd, cwd=root, check=True)
    except subprocess.CalledProcessError:
        ctx.fatal('TypeScript compilation failed')
