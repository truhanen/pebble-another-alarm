# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Pebble watchapp (C + Pebble SDK) scaffold, tooled the same way as the
sibling `pebble-another-timer` project: phone-side logic is written in
TypeScript, compiled to PebbleKit JS, with a Clay-based config page. As of
now the app itself has no real functionality yet — `src/c/pebble-another-alarm.c`
is still the unmodified `pebble new-project` boilerplate (a window with three
button click handlers), and the Clay config has a single placeholder
`ExampleSetting` toggle. Building out actual alarm behavior is the next piece
of work.

## Build & test commands

```bash
npm install
pebble build                 # runs tsc (src/ts -> src/pkjs) via wscript hook, then bundles
pebble install --emulator emery

npm run typecheck            # tsc --noEmit
npm test                     # node --test tests/*.test.js (runs pretest: tsc first)
```

`src/pkjs/*.js` is **generated and gitignored** — always edit `src/ts/*.ts`,
never `src/pkjs/`. `pebble build` regenerates it via `tsc` (config in
`tsconfig.json`, target ES5/CommonJS) before the SDK bundles it; a type error
aborts the build (`noEmitOnError`).

Other Makefile targets: `make clean`, `make kill_emulator`, `make wipe_emulator`,
`make build_and_install_emulator`, `make install_cloudpebble`.

## Architecture

### Watch side (`src/c/`)
- **`pebble-another-alarm.c`** — currently the stock `pebble new-project`
  boilerplate. No alarm logic, no persistence, no AppMessage handling yet.

### Phone side (`src/ts/`)
- **`config_clay.ts`** — the Clay config-page schema. Currently a single
  `ExampleSetting` toggle, standing in for real alarm settings to be
  designed later.
- **`dict.ts`** — pure `buildDict()` transform from Clay settings to an
  AppMessage dict, kept free of Pebble-API calls so it's unit-testable
  without mocking the runtime (see `tests/dict.test.js`).
- **`index.ts`** — entry point; wires `Pebble.addEventListener` for
  `showConfiguration`/`webviewclosed`, opens the Clay URL, and sends the
  built dict on save. No watch-originated sync yet (nothing to sync).
- **`types/pebble.d.ts`** / **`types/pebble-clay.d.ts`** — ambient type
  declarations for the `Pebble` global and the untyped `pebble-clay` package.

AppMessage keys are declared in `package.json` under `pebble.messageKeys`
(currently just `ExampleSetting`) and used as `MESSAGE_KEY_*` in C once the
watch side actually reads them.

## Tests

- `tests/dict.test.js` — Node's built-in `node:test` + `node:assert`, run
  against **compiled** `src/pkjs/dict.js` (not `src/ts` directly), which is
  why `npm test` has a `pretest: tsc` step.
- No C-side test scaffold yet (`pebble-another-timer`'s
  `tests/test_timer_calc.c` pattern — a plain-`assert` program linking a pure
  C core directly, no Pebble SDK needed) — add one once there's a pure,
  host-testable core (e.g. an `alarm_calc.c`) to test.
