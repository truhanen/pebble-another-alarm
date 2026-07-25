# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Pebble watchapp (C + Pebble SDK, emery/Pebble Time 2 only) implementing a
multi-alarm clock: an on-watch list of alarms with time/repeat/snooze/
vibration/sound settings, a wakeup-driven ring screen, and a phone-side
Clay config page for global settings (first day of week, vibration pattern,
default snooze). Full design rationale lives in `SPEC.md` — this file covers
what's actually implemented and where.

## Build & test commands

```bash
npm install
pebble build                 # runs tsc (src/ts -> src/pkjs) via wscript hook, then bundles
pebble install --emulator emery

npm run typecheck            # tsc --noEmit
npm test                     # node --test tests/*.test.js (phone-side; runs pretest: tsc first)

# pure C core, no Pebble SDK needed:
gcc -I src/c tests/test_alarm_calc.c src/c/alarm_calc.c -o /tmp/t && /tmp/t
```

`src/pkjs/*.js` is **generated and gitignored** — always edit `src/ts/*.ts`,
never `src/pkjs/`. `pebble build` regenerates it via `tsc` (config in
`tsconfig.json`, target ES5/CommonJS) before the SDK bundles it; a type error
aborts the build (`noEmitOnError`).

Other Makefile targets: `make clean`, `make kill_emulator`, `make wipe_emulator`,
`make build_and_install_emulator`, `make install_cloudpebble`.

## Emulator testing (headless)

Observed on **Pebble Tool v5.0.39, active SDK v4.17** (`pebble --version`).
This section documents pebble-tool's *own* implementation quirks (verified by
reading its Python source), not a stable Pebble platform fact — re-check
`pebble --version` before trusting this, and if it's changed, re-verify the
VNC-mismatch/kill-on-flag-change behavior in `pebble_tool/sdk/emulator.py`
before relying on the steps below, since a tool upgrade could easily have
fixed or changed this.

Before a new round of manual testing (button-driven UI testing, wakeup/alarm
timing tests, etc.), kill and wipe the emulator first, then relaunch it with
`--vnc` on every command that touches it:

1. `pebble kill` — kills whatever emulator(s) are running (no
   `--emulator`-scoped kill exists; it's global).
2. `pebble wipe` — clears persisted app storage for the current SDK's
   platforms (add `--everything` only if you also want to wipe other SDK
   versions / log out; not needed for routine test resets).
3. `pebble install --emulator emery --vnc` — relaunches the emulator with VNC
   enabled and installs the build.
4. From then on, pass `--vnc` on **every** subsequent command that touches
   this emulator in the same session: `pebble screenshot --vnc --no-open
   <path>`, `pebble emu-button --vnc ...`, `pebble emu-set-time --vnc ...`,
   etc. Dropping `--vnc` on any one of them risks the tool detecting a
   VNC-state mismatch and silently restarting QEMU underneath you, resetting
   the clock and losing in-memory app state.
5. Only one Pebble tool connection to the emulator works reliably at a time —
   don't run `pebble logs` as a separate concurrent background process while
   also issuing `install`/`screenshot`/`emu-button` calls; it conflicts and
   both sides time out. Use `pebble install --vnc --logs` to capture the
   immediate post-install `init()` log output in the same connection instead.
6. Screenshots actually work with `--vnc` in this headless environment (plain
   `pebble screenshot` times out with no window server) — use them to
   navigate the UI visually instead of pressing buttons blind.

## Test hooks (APP_TEST_HOOKS)

Setting up a specific alarm state through the on-watch UI (the multi-step
"+ New alarm" wizard, or worse, waiting for real wall-clock time to pass to
reach an "already fired" / "currently snoozed" state) is slow and some states
aren't reachable through the UI at all. `main.c`'s `handle_test_message()`
adds a phone→watch AppMessage channel that creates or edits an alarm directly
from a single message — including state no UI path can produce.

This is compiled in **only** when built with `APP_TEST_HOOKS=1`:

```bash
APP_TEST_HOOKS=1 pebble build          # or: make build_test
make build_and_install_emulator_test   # build + install in one step
```

A plain `pebble build`/`make build`/`make build_and_install_emulator` never
defines `APP_TEST_HOOKS`, so a normal build's `inbox_received` doesn't contain
this code at all — no paired phone can reach it. The message keys themselves
are always declared (`package.json`'s `messageKeys`, harmless — they're just
unused integer IDs without the handler), only the watch-side handling is
gated.

Send messages with `pebble send-app-message` (numeric key IDs only, no
symbolic names — note `--int KEY=VALUE [KEY=VALUE ...]` takes **one** flag
with all pairs space-separated; repeating `--int` overwrites earlier pairs
instead of accumulating them):

```bash
pebble send-app-message --emulator emery --vnc \
  --int 10008=0 10009=8 10010=30 10011=0 10012=0 10013=1 10014=5 10015=2 10019=120 10020=1 \
  --string 10018=TestAlarm
```

Key IDs are assigned by `pebble build` in two passes, **not** simply
`10000 + declaration index`: every array-syntax key (`"Name[N]"`, e.g.
`DefaultAlarmSignal[2]`) claims its N consecutive ids *first*, regardless of
where it sits in the `messageKeys` list, and only then do the plain scalar
keys get assigned the remaining ids in declaration order. Concretely,
`DefaultAlarmSignal[2]` claims 10000-10001, which is why the table below
starts at 10002 rather than 10000 — adding or resizing any array-type key
shifts every scalar key's id after it. Always regenerate and check
`build/src/message_keys.auto.c` after touching `messageKeys` rather than
trusting a mental model of the numbering.

| Key | ID | Type | Meaning |
|---|---|---|---|
| `TestClearAlarms` | 10007 | int, nonzero | Wipes all alarms (`s_count = 0`) before anything else in the same message. |
| `TestAlarmIndex` | 10008 | int | **Required** to touch alarm fields. `== s_count` appends a new alarm (seeded with the same defaults as the "+ New alarm" wizard — enabled, vibration+sound on, default snooze, one-time — except the time, which is a fixed 7:00 here rather than "next minute", for a deterministic default unless overridden); `< s_count` edits that existing alarm in place; anything else is ignored (logged as a warning). |
| `TestAlarmHour` | 10009 | int 0-23 | |
| `TestAlarmMinute` | 10010 | int 0-59 | |
| `TestAlarmRepeats` | 10011 | int 0/1 | |
| `TestAlarmRepeatDays` | 10012 | int 0-127 | `AC_DAY_*` bitmask. |
| `TestAlarmEnabled` | 10013 | int 0/1 | |
| `TestAlarmSnoozeMinutes` | 10014 | int | |
| `TestAlarmSnoozeMax` | 10015 | int | 0 = unlimited. |
| `TestAlarmVibrationEnabled` | 10016 | int 0/1 | |
| `TestAlarmSoundEnabled` | 10017 | int 0/1 | |
| `TestAlarmName` | 10018 | string | |
| `TestAlarmSnoozeInSec` | 10019 | int | Seconds from *now* until the snooze deadline — an offset, not an absolute epoch, so a test harness doesn't need to know the device's clock. 0 clears/means not snoozed. |
| `TestAlarmLastFiredToday` | 10020 | int 0/1 | Stamps the opaque `last_fired_day` with today's day id (simulates "this alarm's regular occurrence already fired today") or clears it to "never" — a state otherwise only reachable by waiting for real time to cross the alarm's hour:minute. |
| `TestAlarmPending` | 10021 | int 0/1 | Forces `alarm_pending`, jumping straight to "ring screen should show" without waiting for a real due transition. |
| `TestAlarmIsCron` | 10022 | int 0/1 | Switches the alarm to cron mode (see "Cron-syntax alarms" below). |
| `TestAlarmCronMinute` | 10023 | string | Raw cron minute field (e.g. `"*/20"`), parsed into `cron_min_mask`. Logs a warning (field left as previously set) if unparseable. |
| `TestAlarmCronHour` | 10024 | string | Raw cron hour field, parsed into `cron_hour_mask`. |
| `TestAlarmCronDow` | 10025 | string | Raw cron day-of-week field (range 0-6), parsed into `repeat_days` (reused as the cron dow mask — see below). |
| `TestAlarmCronFiredNow` | 10026 | int 0/1 | Minute-granularity sibling of `TestAlarmLastFiredToday`: stamps `cron_last_fired_min` with the current epoch-minute (simulates "already fired this exact minute") or clears it to "never". |
| `TestAlarmAutoStop` | 10027 | int 0/1 | Sets `auto_stop` (see "Key design points" below). |

Only keys present in the message are applied — anything omitted is left
untouched on an existing alarm (or defaulted, per above, on a new one).
Applying any field calls `persist_all(); rearm_wakeup(); reload_ui();`, same
as every other alarm-mutating code path.

## Architecture

### Watch side (`src/c/`)

- **`alarm_calc.c`/`.h`** — the pure, host-testable core. Owns the `Alarm`
  struct, weekday-bitmask helpers, `ac_next_offset_days`/`ac_next_occurrence`
  (day-offset and cross-alarm "which fires soonest" math, operating on
  caller-supplied wall-clock fields — no `<time.h>` calendar calls, so it's
  fully testable with plain ints), `ac_mark_fired` (advances a fired alarm's
  schedule: one-time alarms auto-disable, a set `skip_next` is consumed), the
  `ac_format_time`/`ac_format_repeat_summary` display helpers, and the
  parallel `ac_cron_*` family (`ac_cron_parse_field`/`ac_cron_next_offset_days`/
  `ac_cron_is_due`/`ac_format_cron_summary`) for cron-syntax alarms — see
  "Cron-syntax alarms" below. No Pebble SDK dependency —
  `tests/test_alarm_calc.c` links it directly.
- **`alarm_store.c`/`.h`** — persistence only: one `Alarm` per persist key
  (`PERSIST_KEY_ALARM_BASE + i`), plus scalar keys for schema/count/wakeup id
  and the phone-configured globals (first day of week, vibe pattern, default
  snooze minutes/max). No business logic.
- **`main.c`** — the monolithic UI/controller (mirrors
  `pebble-another-timer`'s `main.c` structure): the main list `MenuLayer`,
  the wakeup scheduling (`rearm_wakeup`/`compute_next_fire_time`/
  `sweep_due_alarms`), the full-screen ring window, four reusable
  field-editor windows (time, repeat/weekday, snooze, cron), a generic
  2-choice confirm window, the per-alarm edit menu, and the "+ New alarm"
  creation wizard. State lives in static globals (`s_alarms`, `s_count`,
  `s_snooze_deadline`/`s_snooze_count` — the latter two in-memory only, not
  persisted, per the snooze-reset rule below).
- **`multitap_keyboard/`** is a vendored third-party widget (Apache-2.0,
  `multitap_keyboard/LICENSE`), copied from `pebble-another-timer` — don't
  restyle its internals to match the rest of the codebase; treat it as
  upstream. Used only for the alarm label text-entry step.
- No `worker_src/` — there is no background-worker binary.

### Phone side (`src/ts/`)

- **`config_clay.ts`** — the Clay config schema: `FirstDayOfWeek`
  (radiogroup, Sunday/Monday), `AlarmVibePattern` (select, Double/Short/
  Long), `AudioVolume` (slider, 0-100, 0=disabled — see sound design point
  below), `DefaultAlarmSignal` (checkboxgroup, "Defaults for new alarms" —
  Vibration/Sound, pre-fills a new alarm's own toggles; see
  `messageKeys`/array-key note below), `DefaultSnoozeMinutes`/
  `DefaultSnoozeMax` (number inputs).
- **`dict.ts`** — pure `buildDict()` transform, `parseInt`s each Clay string
  value into a real int32. Because `index.ts` builds the AppMessage dict
  manually (`autoHandleEvents: false`), the watch reads plain ints directly —
  no ASCII-digit-unwrap workaround needed for `select`/`radiogroup` values.
  `AudioVolume` comes back from Clay as a number (not a string, since it's a
  `slider`), so `toInt()` stringifies before parsing. `DefaultAlarmSignal`
  comes back as an array of booleans (Clay's `checkboxgroup` type);
  `buildDict` takes a second `keys` param (`{ DefaultAlarmSignal: number }`,
  the key's base numeric id) and sends its two elements as **two ordinary
  scalar ints at explicit numeric keys** — `dict[keys.DefaultAlarmSignal]` =
  Vibration, `dict[keys.DefaultAlarmSignal + 1]` = Sound — not as one key
  holding a JS array value (see the AppMessage keys note below for why).
  `keys` is a plain parameter rather than something `dict.ts` looks up
  itself, so it stays a pure function of its inputs and testable without the
  generated `message_keys` module.
- **`index.ts`** — entry point; wires `Pebble.addEventListener` for
  `showConfiguration`/`webviewclosed`, opens the Clay URL, and sends the
  built dict on save. Imports the generated `message_keys` module (typed by
  `types/message-keys.d.ts`; resolved to `build/js/message_keys.json` by the
  SDK's webpack config, so it only exists at build time, not at typecheck
  time against a real file) to get `DefaultAlarmSignal`'s base id for
  `buildDict`. Alarms themselves are **never synced to/from the phone** —
  only these global settings are. All alarm data (time, label, repeat,
  snooze, per-alarm toggles) lives solely in the watch's persistent storage,
  edited entirely through the watch UI.

AppMessage keys (`package.json`'s `pebble.messageKeys`, used as
`MESSAGE_KEY_*` in C): `FirstDayOfWeek`, `AlarmVibePattern`,
`DefaultSnoozeMinutes`, `DefaultSnoozeMax`, `AudioVolume`,
`DefaultAlarmSignal[2]` — phone→watch only. `DefaultAlarmSignal[2]` is
declared with Clay's array-key syntax (required for its `checkboxgroup`
component, which returns a JS array) and reserves **two** consecutive ids —
but that's for Clay's own internal bookkeeping of the component's declared
length, not the wire format we actually use. A real bug fix, in two stages:
the first fix read `MESSAGE_KEY_DefaultAlarmSignal` and `+ 1` as two
separate int32 keys, which happened to be wrong because `index.ts` builds
and sends the dict itself (`autoHandleEvents: false`) rather than letting
Clay auto-submit, and a JS array *value* given to `Pebble.sendAppMessage`
isn't expanded across an array-type key's reserved ids — pypkjs (this
project's emulator JS runtime, `pypkjs/javascript/pebble.py`) instead packs
it into a single `TUPLE_BYTE_ARRAY` tuple at the base id. That got "fixed"
to read one byte-array tuple instead — but that fix rested entirely on
pypkjs's specific behavior, with no way to confirm real mobile PebbleKitJS
(a wholly separate codebase) handles a JS array value in `sendAppMessage`
the same way. The actual fix: stop sending a JS array value at all.
`index.ts` resolves `DefaultAlarmSignal`'s base id via the generated
`message_keys` module and `dict.ts` sends its two elements as **plain
scalar ints at explicit numeric keys** (base and base+1) — this is standard,
unambiguous PebbleKitJS behavior (addressing a message key by its raw
numeric id) with no dependency on how any particular runtime packs array
*values*, so `inbox_received` goes back to reading two ordinary int32 keys.
Declaring `DefaultAlarmSignal[2]` still shifts every other key's numeric id
even though its wire value isn't split across them — never assume
`10000 + declaration index` without checking
`build/src/message_keys.auto.c`.

## Key design points worth knowing before touching this code

- **Wakeup scheduling**: exactly one `WakeupId` is armed at a time, for
  whichever alarm (or active snooze) fires soonest — same "arm the new one
  before cancelling the old" ordering as timer's `rearm_wakeup`, so a
  transiently-refused reschedule can never leave the app with zero wakeups
  armed. Call `rearm_wakeup()` after every mutation.
- **`wakeup_service_subscribe(handle_wakeup_event)`** (in `init()`) is a real
  bug fix, not boilerplate: `wakeup_get_launch_event()` only covers a wakeup
  that had to *relaunch* the app because it wasn't running. If the app was
  already open (foreground) when the armed wakeup fired, there was no handler
  at all for that case — confirmed via the test hooks that a wakeup firing
  while the app stayed open silently did nothing (no ring, no state change)
  until the app was later closed and reopened. `handle_wakeup_event()` runs
  the same sweep/rearm/ring-trigger sequence `init()` does.
- **`snooze_until`/`snooze_count` are persisted fields on `Alarm` itself, not
  in-memory statics** — a real bug fix, not a stylistic choice: a wakeup
  fully relaunches the app (a fresh `init()`), so an in-memory-only deadline
  gets wiped before it can ever be checked against "now", making a snoozed
  alarm ring immediately on the next open instead of waiting out the snooze.
  `snooze_count` resets (via `ac_mark_fired`) at the exact moment
  `sweep_due_alarms()` detects a *regular* (non-snoozed) occurrence becoming
  due — not on any fixed wall-clock boundary, and not on every app restart.
- **`last_fired_day` is also a persisted field on `Alarm`** (stamped by
  `ac_mark_fired`, checked by `ac_is_due`) — another real bug fix: `ac_is_due`
  alone has no sense of "already handled today", since it just checks
  time-of-day-passed + day-matches, which for a repeating alarm stays true
  for the rest of the day. `alarm_pending` isn't enough to guard against a
  re-ring either, since stopping the alarm clears it — without
  `last_fired_day`, simply reopening the app later the same day (any manual
  open, not just a wakeup) would re-trigger a just-stopped repeating alarm.
  `now_day_id()` (`main.c`) turns local wall-clock time into the opaque day
  id `ac_is_due`/`ac_mark_fired` compare — alarm_calc.c itself stays
  `<time.h>`-free per its host-testable-core design.
- **`resync_last_fired_for_schedule_change()`** (`main.c`) is called from
  every place that mutates an alarm's hour/minute/repeats/repeat_days, or
  re-enables it (`edit_on_time_confirm`, `edit_on_repeat_confirm`,
  `edit_select`'s ENABLE row handler, `new_alarm_label_done` at the end of
  the "+ New alarm" wizard) — another real bug fix, and the second half of
  the `last_fired_day` story above: `ac_is_due` only checks "time-of-day
  passed" + "not already fired today", with no notion of *which* occurrence
  that time-of-day belongs to. So a `repeat_days == 0` (no specific day)
  one-time alarm created or edited to an hour:minute already passed today —
  or a repeating/specific-weekday alarm edited to an already-passed time on
  a day it does repeat — looks immediately due the instant the app is next
  opened, even though `ac_next_offset_days` (used for *scheduling* the
  wakeup) correctly works out the real next occurrence is tomorrow (or later
  for a specific weekday). `resync_last_fired_for_schedule_change` mirrors
  `ac_next_offset_days`'s verdict into `last_fired_day`: stamps today's day
  id (blocking today) unless today's offset is genuinely 0. An earlier, narrower
  version of this fix only cleared `last_fired_day` to -1 on re-enable (to
  fix re-enabling-then-picking-a-later-time-today) — but that unconditional
  clear is exactly what caused *this* bug for re-enabling without changing
  an already-past time (or any other schedule edit to an already-past time);
  the resync function fixes both at once.
- **One-time alarms auto-disable, never auto-delete**, once fired
  (`ac_mark_fired`) — kept in the list, greyed out, re-editable.
- **`snooze_minutes == 0` means snooze is disabled outright** (distinct from
  `snooze_max == 0`, which means *unlimited* snoozes) — `alarm_snooze()`
  checks this before its `snooze_max`/`snooze_count` check and, if it's set,
  behaves like Stop instead, same as running out of snoozes does. The snooze
  editor lets `Minutes` go down to 0 (`Repeats` already went down to 0 for
  "unlimited") and the boxes themselves spell out "Disabled"/"Infinite" at 0
  instead of a bare "0", since the two fields mean opposite things there.
- **Alarm sound is a direct port of `pebble-instant-timer`'s "Add alarm
  audio" commit** (`alarm_play_audio()`, `main.c`): the exact same
  beep-silence-beep-silence `SpeakerNote` sequence (MIDI note 95/B6, square
  wave, 150ms beeps / 100ms silence), the same `#if PBL_SPEAKER` guard, and
  the same `speaker_is_muted()` check. The one deliberate difference: Instant
  Timer has no per-alarm concept, so its volume slider (`audioVolume`,
  0-100, 0=disabled) is the *only* gate; here it's a **global** volume
  (`s_audio_volume`, phone-configured via `AudioVolume`) layered under each
  alarm's own `sound_enabled` toggle — both must allow sound for it to play.
- **`auto_stop` alarms still show the ring screen, but only briefly**:
  `trigger_alarm()` branches on `a->auto_stop` — an auto-stop alarm fires
  vibration/sound (via its own existing `vibration_enabled`/`sound_enabled`
  toggles) **exactly once** instead of calling `alarm_buzz_start()`'s
  repeating 4s cadence, and arms a one-shot `s_alarm_auto_stop_timer`
  (`ALARM_AUTO_STOP_MS`, ~2s) that calls `alarm_do_stop()` — the same
  end state as the user pressing Stop — instead of waiting indefinitely.
  `alarm_stop`/`alarm_snooze` are refactored so `alarm_stop` is a thin
  wrapper around `alarm_do_stop()` (shared by the timer callback
  `alarm_auto_stop_cb`), and manually pressing Stop or Snooze during that
  couple-second window cancels the pending timer first
  (`alarm_cancel_auto_stop()`, also called from `alarm_window_unload` as a
  safety net, and defensively at the top of every `trigger_alarm()` call so
  a stale timer can never fire against whichever alarm gets shown next) —
  otherwise it could fire moments later and clobber a snooze the user just
  set, or re-run the "show next pending" chain a second time. Toggled via
  the edit menu's `EDIT_ROW_AUTO_STOP` ("Auto-stop" On/Off) row, alongside
  Vibration/Sound; no wizard step, defaults to off (like every other bool
  field not driven by a phone-config default).
- **Dismissing an alarm exits straight to the watchface if the app wasn't
  already open**: `s_launched_by_wakeup` (`main.c`, set in `init()` from
  `wakeup_get_launch_event()`'s return value) is true only when this whole
  app process exists purely because a wakeup relaunched it from closed —
  not when it was manually opened, and not when a wakeup instead fires
  while the app is already foregrounded (`handle_wakeup_event`, which never
  touches this flag). `alarm_do_stop()`/`alarm_snooze()` both funnel through
  one shared `alarm_finish_ring()` tail: once `show_next_pending_alarm()`
  says there's nothing left to chain to, it calls `window_stack_pop_all()`
  (exiting the app back to the watchface — the same "closest thing to
  never having opened it" mechanism used elsewhere) instead of the usual
  `window_stack_remove(s_alarm_window, true)` (which falls back to the main
  list) when `s_launched_by_wakeup` is set. This applies to Stop, Snooze,
  and an `auto_stop` alarm's timer-driven auto-dismiss alike, since all
  three now route through `alarm_do_stop()`/`alarm_finish_ring()`. Multiple
  alarms due in the same wakeup-launched session still chain through their
  ring screens one at a time as before — the exit only happens once the
  *last* one is dismissed.
- **`repeats` (bool) is separate from `repeat_days` (bitmask)**: `repeats`
  says whether the alarm recurs weekly forever or fires once; `repeat_days`
  says which day(s) apply in either case. This lets a *non-repeating* alarm
  still target a specific weekday (`repeats=false`, one bit set in
  `repeat_days` — "fire once, next Friday") instead of only ever meaning
  "next occurrence of the time, today/tomorrow" (`repeat_days==0`).
  `ac_next_offset_days` scans `repeat_days` the same way regardless of
  `repeats`; only `ac_mark_fired` (disable vs. keep recurring) and
  `skip_next` (repeats-only) branch on it. Toggling the repeat editor's
  ON/OFF box only flips `repeats` — it no longer forces `repeat_days` to
  all-or-nothing, so a picked weekday survives switching repeat on/off
  (except turning repeat on with no day picked yet, which defaults to every
  day so a repeating alarm never ends up with zero days set).
- **Ring screen click config is intentionally asymmetric**: DOWN needs
  `window_multi_click_subscribe` (double-click to stop) while UP is a plain
  single-click (snooze). BACK is explicitly subscribed to a no-op handler —
  leaving it unbound would let Pebble's default "pop the window" behavior
  silently dismiss the alarm, which is the opposite of the intended
  "BACK does nothing, ringing continues" behavior.
- **No touch-dial reuse**: time/duration entry uses plain button-driven
  pickers (UP/DOWN adjust, SELECT advance/confirm, BACK retreat/cancel),
  not a port of the vendored `touch_dial` widget.
- **Time/snooze/repeat editors all follow the same SELECT-advances/BACK-
  retreats pattern**, each a plain custom `Layer` (not a `MenuLayer` — see
  below): SELECT moves forward one field/box, confirming (submitting) when
  already on the last one; BACK moves back one field/box, **cancelling**
  once already on the first one. What cancelling does depends on the
  caller-supplied `on_cancel` (a 4th param on `time_edit_window_push`/
  `repeat_edit_window_push`/`snooze_edit_window_push`, alongside the existing
  `on_confirm`): if non-NULL, it's called instead of `on_confirm` — used by
  the "+ New alarm" wizard (`new_alarm_wizard_cancel`) so exiting *any* of
  its four screens (time, repeat, snooze, or BACK on the label's multitap
  keyboard) aborts the whole flow outright, discarding the draft; the alarm
  is only actually created by completing the last screen (the label) via
  SELECT. If `on_cancel` is NULL — every per-alarm edit-menu call site
  (`edit_on_*_confirm`) — cancelling instead reverts to the value the screen
  was opened with and still calls `on_confirm` with that original value, so
  editing a single field of an already-existing alarm behaves as a no-op on
  cancel rather than needing its own special case.
- **Repeat editor is 8 selectors on one compressed line**, not a `MenuLayer`
  list: selector 0 is the ON/OFF (`repeats`) toggle (given 2 column-units'
  worth of width, weekdays 1 unit each, so "OFF" always fits without
  ellipsizing), selectors 1-7 are the individual weekdays starting at the
  configured first-day-of-week. BACK/SELECT move the cursor horizontally
  across the 8 selectors (not vertically like a normal menu); UP/DOWN toggle
  whichever one is under the cursor. Each selector is GOTHIC_28_BOLD text
  with a thick underscore under it when "on" (unpicked weekdays dim to gray
  while repeat is off); cursor focus is a small downward-pointing chevron
  drawn a few px above the selector (`repeat_draw_focus_marker`), independent
  of the underscore. Turning repeat OFF deselects all weekdays; conversely,
  toggling off the last remaining selected weekday directly, while repeat is
  ON, turns repeat itself OFF — the ON/OFF switch is the source of truth for
  "zero days selected", not an invalid state to silently repair (this
  replaced an earlier "snap back to every day" behavior). While repeat is
  OFF, weekday selectors are
  mutually exclusive (radio-button, not checkbox): picking one clears
  whichever other day was previously picked, since a one-time alarm only
  ever targets a single day. Long-pressing SELECT submits the current
  selections outright regardless of cursor position (a shortcut past
  "advance to the last box first"); long-pressing BACK is an explicit no-op.
- **Main list SELECT is split short-press/long-press** (`ml_select`/
  `ml_select_long`): short-press toggles enable/disable directly, without
  opening the edit menu — same underlying logic as the edit menu's own State
  row (`ml_toggle_enable` mirrors `edit_select`'s `EDIT_ROW_ENABLE` case,
  including the "Disable" vs "Skip next occurrence" prompt when the target
  is a currently-enabled repeating alarm, so one accidental press can't
  silently cancel every future occurrence). Long-press opens the full edit
  menu (`open_edit_window`), which every other field lives behind now. The
  "+ New alarm" row only responds to short-press (starts the wizard);
  long-press on it is an explicit no-op, not "open some nonexistent edit
  menu for it".
- **The "Skip next occurrence" choice toggles `skip_next`, both ways**: if a
  skip is already pending, the prompt's second option relabels itself to
  "Enable next occurrence" (`ml_toggle_enable`/`edit_select`'s
  `EDIT_ROW_ENABLE` case pick the label from the alarm's current
  `skip_next`), and choosing it clears `skip_next` rather than setting it
  again — the same choice slot undoes what it set, instead of needing a
  separate "un-skip" affordance. The edit menu's State row surfaces this
  too: it reads "Skip next" (not "Enabled") whenever `enabled && repeats &&
  skip_next`, so the pending skip isn't otherwise invisible until its
  occurrence silently doesn't ring.
- **Visual style matches `pebble-another-timer`**, ported deliberately:
  - The main list (`ml_row_colors`/`ml_draw_alarm_row`) mirrors timer's
    `ml_row_colors`/`ml_draw_row` — per-row state tinting (snoozing=red,
    enabled=green, disabled=white; selected rows get a darker/black variant
    of the same color), a fixed-width bold time followed by a lighter-weight
    label, and a divider line under each row. Row order is by raw clock time
    (hour:minute) ascending, ties broken by index — not time-to-next-
    occurrence, so a snoozed or disabled alarm still sorts by its own
    hour:minute like any other. Each row's first line also gets a leading
    play/stop icon (`ml_draw_state_icon`), a direct port of timer's own
    `ml_draw_state_icon`'s `TS_RUNNING`/`TS_STOPPED` cases (scanline-built
    triangle / filled square) — play when `enabled`, stop when disabled or
    when `repeats && skip_next` (mirrors the edit menu's "Skip next"
    condition exactly). Drawn in the row's existing `fg` tint, with the
    time text shifted right 16px to make room; line 2 is unaffected and
    stays flush-left.
  - The main window has a bottom bar (`draw_bottom_bar`/
    `bottom_bar_rect_for_bounds`), a direct port of timer's own bottom bar:
    a thin divider line + black strip pinned to the bottom, reducing the
    `MenuLayer`'s height by `BOTTOM_BAR_H` rather than overlaying it. Current
    time (system 12h/24h-aware `clock_copy_time_string`) on the left; the
    next scheduled alarm's due time on the right via `compute_next_fire_time`
    — "Next: HH:MM" (no day label) if it lands on today's calendar date,
    "Next: <Day> HH:MM" otherwise, including tomorrow (e.g. "Next: Sat
    06:00" — just the weekday abbreviation, no separate "Tomorrow" case), or
    "Next: --" if nothing is scheduled. Refreshed once a minute via
    `tick_timer_service_subscribe`
    (`handle_minute_tick`), guarded on the main window actually being the
    top of the window stack.
  - Itemized menus (`confirm_window`, `repeat_window`, `edit_window`) use
    `menu_layer_set_normal_colors`/`menu_layer_set_highlight_colors`
    (white/black, black/white) instead of manually painting each row —
    exactly timer's simpler "detail window" pattern (`dl_draw_row`), not the
    main list's per-row tinting. `edit_window` has no header — the label is
    its own "Label: <name>" row (opens the multitap keyboard) alongside
    State/Time/Repeat/Snooze/Vibration/Sound/Auto-stop/Delete.
  - The time and snooze editors both render through the shared
    `dial_draw_number_boxes`/`dial_box_area` helpers, a direct port of
    timer's non-touch box-type duration dial (`dial_update_proc`,
    `dl_draw_triangle_up_sized`/`_down_sized`): bordered boxes, hand-drawn
    up/down triangles, GOTHIC_28_BOLD digits with the same optical-centering
    "rise" correction, and a header showing the field's current value. The
    snooze editor's two fields (duration/max) have no natural combined
    "full value" the way H:M:S does, so its header shows whichever field is
    currently active instead.
- **`timeline_pin_enabled`** exists in the `Alarm` struct and is persisted,
  but has no UI row and no push logic — pushing a real Pebble timeline pin
  needs a server-side API key this repo doesn't have (see `SPEC.md` §9.2).

## Cron-syntax alarms

A third alarm schedule shape, alongside the fixed hour:minute+`repeat_days`
legacy mode: `is_cron` alarms are defined by three independent cron-style
fields — minute (0-59), hour (0-23), day-of-week (0-6) — each written as
`*`, `N`, `N-M`, `*/N`, `N-M/N2`, or a comma-list of these (e.g. `"*/20"`,
`"9-17/2"`, `"1,5,10-15"`), parsed by `ac_cron_parse_field` into a bitmask.
Deliberately **3 fields only** (no day-of-month/month — this app has no
calendar-date concept anywhere, and building one just for this feature was
rejected in favor of reusing the weekday-bitmask model already in place).

- **True multi-fire semantics, a deliberate departure from every other
  alarm in the app**: a cron alarm can ring many times a day if its pattern
  matches many times (e.g. `*/20` rings every 20 minutes). There is
  **no suppression window** after stopping/snoozing a firing — the very
  next matching minute fires again. This is intentional, not a bug to later
  "fix" (see the comment at `sweep_due_alarms()`'s cron branch, `main.c`).
- **Day-of-week reuses `repeat_days`/`AC_DAY_BIT`** — no separate field:
  when `is_cron`, `repeat_days` holds the parsed cron dow mask instead of
  the legacy weekly-repeat days, and `repeats`/`skip_next` are unused
  (forced false/ignored). Every legacy code path touching those fields must
  branch on `is_cron` — missing a guard silently corrupts a cron alarm's
  display or scheduling. The one non-obvious spot: the edit menu's
  ENABLE-row re-enable handler used to call
  `resync_last_fired_for_schedule_change()` (which reads `repeat_days` under
  legacy semantics) unconditionally — it now branches on `is_cron` first.
- **No eager-fire guard needed on create/edit**, unlike legacy's
  `resync_last_fired_for_schedule_change`: for cron, "the pattern currently
  matches right now" **is** the real, immediate next occurrence, so every
  cron create/edit path just sets `cron_last_fired_min = -1`. `main.c`'s
  `sweep_due_alarms()` dedups on exact epoch-minute
  (`ac_cron_is_due`/`cron_last_fired_min`) rather than `last_fired_day`'s
  whole-day dedup, since a cron alarm can genuinely fire many times a day.
- **Secret UP+DOWN chord on the time editor** switches it to cron entry.
  Detected via `window_raw_click_subscribe` held-flags (`s_time_up_held`/
  `s_time_down_held` in `main.c`) added *alongside* the time editor's
  existing `window_single_repeating_click_subscribe` UP/DOWN — confirmed
  against the Pebble SDK header that raw click has no documented conflict
  with single-repeating-click on a different button (unlike single-click vs
  single-repeating-click on the *same* button, which do conflict), so this
  adds zero latency to a normal single click. `time_edit_window_push` gained
  a 5th `on_chord` callback parameter for this.
- **One `cron_edit_window_push()` window serves every call site** — the "+
  New alarm" wizard (chord replaces the Time+Repeat steps, skipping straight
  to Snooze), converting an existing normal alarm (chord on its edit menu's
  Time row — `edit_cron_convert_cancel` is a real no-op cancel, not `NULL`,
  since there's no prior cron state to revert to), and re-editing an
  existing cron alarm (its edit menu's Cron row — real `NULL`
  snapshot-revert cancel semantics apply here). `MenuLayer`-based: row 0
  "Submit", rows 1-3 the three fields (each opens the shared multitap
  keyboard to edit its raw text; live "(invalid)" annotation if
  `ac_cron_parse_field` currently fails, checked on every draw but never
  blocking typing — only Submit is gated on all three parsing cleanly).
  BACK follows the same on_cancel-vs-snapshot-revert convention as the
  time/repeat/snooze editors. Long-press SELECT submits outright regardless
  of cursor position; long-press BACK is a no-op.
- **Edit menu display**: `edit_build_rows()` builds the ordered row-kind
  list for the current alarm (collapsing Time+Repeat into one `EDIT_ROW_CRON`
  row when `is_cron`) so `edit_num_rows`/`edit_draw_row`/`edit_select` share
  one source of truth for "which row is at this menu position" without
  scattering `is_cron` checks through each of them individually.
- **Main list display**: the bold time slot shows "Cron" instead of a
  formatted time; the summary line below shows the three raw cron fields
  space-joined (`ac_format_cron_summary`) instead of the repeat summary.
  Sort key (`cron_sort_key`, display/tie-breaking only, not used for actual
  scheduling): lowest set bit in `cron_hour_mask`/`cron_min_mask` converted
  to `hour*60+minute`, same units as the legacy sort key, so cron and legacy
  alarms interleave in one time-ordered list.
- **`CRON_FIELD_LEN` (28)** caps each raw field string at 27 usable chars —
  comfortably fits comma-lists like `"0-59/5,10,20-25/3"`.
- **Cron field entry uses a fixed numeric keyboard**, not the vendored
  `multitap_keyboard` widget's default ABC layout: `cron_select()` (`main.c`)
  pushes each field via `multitap_keyboard_window_push_numeric()` instead of
  the general-purpose `multitap_keyboard_window_push_ex()` every other
  text-entry call site (the alarm label editor) still uses. This is a
  genuinely new, opt-in *behavior* mode added to the vendored widget itself
  (`multitap_keyboard_set_numeric_mode()`) — not a restyle of it, so it
  doesn't conflict with the "treat it as upstream" note above: it's gated by
  one `bool numeric_mode` field defaulting to `false` (zero effect on every
  other call site), which locks the keyboard to its existing `PAGE_NUMBERS`
  layout (page-cycling via Down or the bottom-left key becomes a no-op —
  `prv_cycle_page`'s single choke point just early-returns) and repurposes
  the bottom-left key (normally Shift) to multi-tap-cycle `*`/`-`/`/`/`,`
  through the same `prv_eff_glyphs`/pending-key/commit machinery every other
  key already uses, rather than a parallel code path — so it inherits the
  usual multi-tap highlight/underline/commit-on-timeout behavior for free.

## Tests

- `tests/test_alarm_calc.c` — plain `assert`-based C program, no framework,
  links `alarm_calc.c` directly (see build command above). Covers one-time/
  repeating occurrence math (today/tomorrow/no-day-this-week/wraparound),
  `skip_next`, `ac_mark_fired`, the format helpers, and the `ac_cron_*`
  family (field parsing incl. every malformed-syntax case, next-occurrence
  math incl. multi-fire progression across successive calls, due-check incl.
  the older-`last_fired_min`-still-due multi-fire case, and summary
  formatting).
- `tests/dict.test.js` — Node's built-in `node:test` + `node:assert`, run
  against **compiled** `src/pkjs/dict.js` (not `src/ts` directly), which is
  why `npm test` has a `pretest: tsc` step.
