# pebble-another-alarm

A Pebble watchapp written in C using the Pebble SDK, with a Clay-based
config page on the phone written in TypeScript.

> **Status:** scaffold only. `src/c/pebble-another-alarm.c` is still the
> stock `pebble new-project` boilerplate and the Clay config page has a
> single placeholder `ExampleSetting` toggle — no real alarm functionality
> exists yet.

## Building & running

```sh
npm install
pebble build                          # compiles src/ts -> src/pkjs (tsc), then bundles
pebble install --emulator emery       # install on the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

See the `Makefile` for other targets (`clean`, `kill_emulator`,
`wipe_emulator`, `install_cloudpebble`, `build_and_install_emulator`,
`build_and_install_cloudpebble`).

## Target platforms

`targetPlatforms` in `package.json` controls which watches you build for.
This project only targets **emery** (Pebble Time 2).

## Phone-side TypeScript, Clay & tests

Phone-side (PebbleKit JS) logic is written in TypeScript under `src/ts/` and
compiled to `src/pkjs/` by a `wscript` build hook — `src/pkjs/*.js` is
generated and gitignored, so always edit `src/ts/*.ts` instead. The config
page itself is built with [Clay](https://github.com/pebble/clay)
(`src/ts/config_clay.ts`), wired up in `src/ts/index.ts`.

```sh
npm install
npm run typecheck                     # tsc --noEmit
npm test                              # compiles TS, then runs tests/*.test.js
```

## Project layout

```
src/c/           C source for the watchapp
src/ts/          Phone-side TypeScript (Clay config + AppMessage), compiled
                 to src/pkjs/ on build
src/pkjs/        Generated PebbleKit JS (gitignored, do not edit directly)
worker_src/c/    Background worker source, if any
resources/       Images, fonts, and other bundled resources
tests/           node:test unit tests, run against compiled src/pkjs/
package.json     Project metadata (UUID, platforms, resources, message keys)
wscript          Build rules — compiles TypeScript, then bundles as usual
```

By default this project is configured as a watchapp. To make it a watchface,
set `pebble.watchapp.watchface` to `true` in `package.json`.

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>
