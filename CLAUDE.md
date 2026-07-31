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

Key IDs are assigned by `pebble build` as `10000 + declaration index` in
`package.json`'s `messageKeys` list — **except** if any key uses Clay's
array syntax (`"Name[N]"`), which claims its N consecutive ids *first*,
regardless of where it sits in the list, before any plain scalar key gets
assigned an id; this codebase has no array-type key at all right now (the
one that used to exist, `DefaultAlarmSignal[2]`, was replaced by two
ordinary scalar toggles — see the AppMessage keys note below), so today the
mapping really is just declaration order. That won't stay true
the moment an array-type key is reintroduced, so always regenerate and
check `build/src/message_keys.auto.c` after touching `messageKeys` rather
than trusting a mental model of the numbering.

| Key | ID | Type | Meaning |
|---|---|---|---|
| `TestClearAlarms` | 10007 | int, nonzero | Wipes all alarms (`s_count = 0`) before anything else in the same message. |
| `TestAlarmIndex` | 10008 | int | **Required** to touch alarm fields. `== s_count` appends a new alarm (seeded with the same defaults as the "+ New alarm" wizard — enabled, vibration+sound on, Double vibe pattern, default snooze, one-time — except the time, which is a fixed 7:00 here rather than "next minute", for a deterministic default unless overridden); `< s_count` edits that existing alarm in place; anything else is ignored (logged as a warning). |
| `TestAlarmHour` | 10009 | int 0-23 | |
| `TestAlarmMinute` | 10010 | int 0-59 | |
| `TestAlarmRepeats` | 10011 | int 0/1 | |
| `TestAlarmRepeatDays` | 10012 | int 0-127 | `AC_DAY_*` bitmask. |
| `TestAlarmEnabled` | 10013 | int 0/1 | |
| `TestAlarmSkipNext` | 10015 | int 0/1 | Sets `skip_next` directly. Reused for cron alarms too (see "Cron-syntax alarms" below) — no separate cron-specific key. |
| `TestAlarmSnoozeMinutes` | 10016 | int | |
| `TestAlarmSnoozeMax` | 10017 | int | 0 = unlimited. |
| `TestAlarmVibrationEnabled` | 10018 | int 0/1 | |
| `TestAlarmSoundEnabled` | 10019 | int 0/1 | |
| `TestAlarmName` | 10020 | string | |
| `TestAlarmSnoozeInSec` | 10021 | int | Seconds from *now* until the snooze deadline — an offset, not an absolute epoch, so a test harness doesn't need to know the device's clock. 0 clears/means not snoozed. |
| `TestAlarmLastFiredToday` | 10022 | int 0/1 | Stamps the opaque `last_fired_day` with today's day id (simulates "this alarm's regular occurrence already fired today") or clears it to "never" — a state otherwise only reachable by waiting for real time to cross the alarm's hour:minute. |
| `TestAlarmPending` | 10023 | int 0/1 | Forces `alarm_pending`, jumping straight to "ring screen should show" without waiting for a real due transition. |
| `TestAlarmIsCron` | 10024 | int 0/1 | Switches the alarm to cron mode (see "Cron-syntax alarms" below). |
| `TestAlarmCronMinute` | 10025 | string | Raw cron minute field (e.g. `"*/20"`), parsed into `cron_min_mask`. Logs a warning (field left as previously set) if unparseable. |
| `TestAlarmCronHour` | 10026 | string | Raw cron hour field, parsed into `cron_hour_mask`. |
| `TestAlarmCronDow` | 10027 | string | Raw cron day-of-week field (range 0-6), parsed into `repeat_days` (reused as the cron dow mask — see below). |
| `TestAlarmCronFiredNow` | 10028 | int 0/1 | Minute-granularity sibling of `TestAlarmLastFiredToday`: stamps `cron_last_fired_min` with the current epoch-minute (simulates "already fired this exact minute") or clears it to "never". |
| `TestAlarmAutoStop` | 10029 | int 0/1 | Sets `auto_stop` (see "Key design points" below). |
| `TestAlarmVibePattern` | 10030 | int 0-2 | 0=Double, 1=Short, 2=Long. Sets the per-alarm `vibe_pattern` (see "Key design points" below); defaults to 0/Double on a newly appended alarm regardless of the phone-configured default, same deterministic-defaults rationale as `TestAlarmIndex`'s other hardcoded fields. |
| `TestAlarmIncreasingVolume` | 10031 | int 0/1 | Sets `increasing_volume` directly (see "Key design points" below). |
| `TestAlarmCronDom` | 10032 | string | Raw cron day-of-month field (range 1-31), parsed into `cron_dom_mask`. See "Cron-syntax alarms" below. |
| `TestAlarmCronMonth` | 10033 | string | Raw cron month field (range 1-12), parsed into `cron_month_mask`. See "Cron-syntax alarms" below. |
| `TestAlarmCronWeek` | 10035 | string | Raw cron ISO week-of-year field (range 1-53), parsed into `cron_week_mask`. See "Cron-syntax alarms" below. |

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

- **`config_clay.ts`** — two sections: (1) "New alarm defaults" —
  everything here only pre-fills a newly created alarm's own fields, never
  touches existing ones: a `DefaultSnoozeEnabled` toggle +
  `DefaultSnoozeMinutes`/`DefaultSnoozeMax` number inputs (the latter
  labeled "Repeats", matching the on-watch snooze editor's own field name
  for the same value — see the snooze design point below),
  `DefaultSoundEnabled`/`DefaultVibrationEnabled` (two independent
  `toggle`s, in that order — Sound above Vibration) and `AlarmVibePattern`
  (select, Double/Short/Long — only seeds a new alarm's own `vibe_pattern`;
  see the per-alarm vibration pattern design point below); (2) "Other" —
  `FirstDayOfWeek` (radiogroup, Sunday/Monday), `DateFormat` (select,
  Day.Month/Month.Day, right after `FirstDayOfWeek` — read live by
  `format_relative_fire_time()`'s date-only tier, `main.c`, since Pebble's C
  API has no date-order equivalent of `clock_is_24h_style()` to fall back
  on for this), and `AudioVolume` (slider, 0-100, 0=disabled — see sound
  design point below), given its own place at the bottom because, unlike
  everything in (1), it's read live by every alarm's ring screen rather
  than copied once at creation.
- **`dict.ts`** — pure `buildDict()` transform, `parseInt`s each Clay string
  value into a real int32. Because `index.ts` builds the AppMessage dict
  manually (`autoHandleEvents: false`), the watch reads plain ints directly —
  no ASCII-digit-unwrap workaround needed for `select`/`radiogroup` values.
  `AudioVolume` comes back from Clay as a number (not a string, since it's a
  `slider`), so `toInt()` stringifies before parsing. `DefaultSoundEnabled`/
  `DefaultVibrationEnabled` come back as plain booleans (Clay's `toggle`
  type) and are sent as ordinary `0`/`1` ints at their own message keys —
  no array-key/checkboxgroup complexity here at all; each toggle is just
  another independent scalar setting, same as every other field.
  `DefaultSnoozeEnabled` (also a `toggle`) is deliberately **not** forwarded
  to the watch as its own wire key — when it's `false`, `buildDict` just
  sends `DefaultSnoozeMinutes` as `0` outright, reusing the watch's existing
  "`snooze_minutes == 0` means disabled" convention (see the snooze design
  point below) instead of adding a flag for the watch to also track and keep
  in sync.
- **`index.ts`** — entry point; wires `Pebble.addEventListener` for
  `showConfiguration`/`webviewclosed`, opens the Clay URL, and sends the
  built dict on save. Alarms themselves are **never synced to/from the
  phone** — only these global settings are. All alarm data (time, label,
  repeat, snooze, per-alarm toggles) lives solely in the watch's persistent
  storage, edited entirely through the watch UI.

AppMessage keys (`package.json`'s `pebble.messageKeys`, used as
`MESSAGE_KEY_*` in C): `FirstDayOfWeek`, `AlarmVibePattern`,
`DefaultSnoozeMinutes`, `DefaultSnoozeMax`, `AudioVolume`,
`DefaultSoundEnabled`, `DefaultVibrationEnabled`, `DateFormat` — phone→watch
only, all plain scalars, no array-type key. `DateFormat` was appended at the
very end of the declared list (after every `Test*` key) rather than next to
`FirstDayOfWeek` despite being thematically related, per the "IDs are
`10000 + declaration index`, never insert" rule below — it landed at 10034.
(`DefaultSnoozeEnabled` is a real Clay
field but intentionally has no corresponding watch-side message key at all
— see `dict.ts` above.) This used to need declaring `DefaultAlarmSignal[2]`
with Clay's array-key syntax for a `checkboxgroup` component, plus a fair
amount of care in both `dict.ts` and `main.c` about exactly how a JS array
*value* gets packed across an array-type key's reserved ids (that behavior
isn't guaranteed portable across every PebbleKitJS runtime) — replacing the
checkboxgroup with two independent `toggle`s removed that whole class of
problem outright, not just worked around it: there's no array-type key left
in this app at all, so every message key is now just an ordinary scalar at
`10000 + declaration index`.

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
  The phone config's "Enable snooze" toggle (`DefaultSnoozeEnabled`) reuses
  this exact convention on the wire rather than adding a second flag: `off`
  just makes `dict.ts` send `DefaultSnoozeMinutes` as `0` outright, so the
  watch never even needs to know the toggle existed.
- **Alarm sound is a direct port of `pebble-instant-timer`'s "Add alarm
  audio" commit** (`alarm_play_audio()`, `main.c`): the exact same
  beep-silence-beep-silence `SpeakerNote` sequence (MIDI note 95/B6, square
  wave, 150ms beeps / 100ms silence), the same `#if PBL_SPEAKER` guard, and
  the same `speaker_is_muted()` check. The one deliberate difference: Instant
  Timer has no per-alarm concept, so its volume slider (`audioVolume`,
  0-100, 0=disabled) is the *only* gate; here it's a **global** volume
  (`s_audio_volume`, phone-configured via `AudioVolume`) layered under each
  alarm's own `sound_enabled` toggle — both must allow sound for it to play.
- **Vibration pattern is a per-alarm attribute (`Alarm.vibe_pattern`), unlike
  volume**: `alarm_vibrate()` takes the pattern as a parameter now instead of
  reading a global, and both ring-time call sites pass the firing alarm's
  own `vibe_pattern`. `s_default_vibe_pattern` (phone-configured via the
  still-named-`AlarmVibePattern` message key) is only read once, at
  creation, to seed a new alarm's `vibe_pattern` (`start_new_alarm_flow`) —
  changing the phone setting afterward has no effect on already-created
  alarms, unlike `AudioVolume`. Editable per-alarm via the edit menu's
  "Vibe pattern" row, which cycles Double -> Short -> Long -> Double on
  SELECT (`EDIT_ROW_VIBE_PATTERN`), the same short-press-cycles convention
  as the main list's enable/disable/skip state — there's no 3-way picker
  widget in this app, so cycling was the natural fit rather than building
  one. Adding this field bumped `STORE_SCHEMA` (`alarm_store.h`) to 7 — the
  baseline version the forward-compatible migration below ships in, so this
  particular bump was the last one to unconditionally wipe existing alarms;
  see the `store_load()` design point below for what happens on the *next*
  bump.
- **A `STORE_SCHEMA` bump doesn't have to wipe every alarm, for a pure
  field-append**: `store_load()` (`alarm_store.c`) relies on
  `persist_read_data`'s own documented behavior — it copies
  `min(buffer_size, actual_stored_size)` bytes and leaves the rest of the
  buffer untouched — so reading an older, shorter `Alarm` blob into a
  `memset`-zeroed current-sized buffer already reproduces every field that
  existed at write time correctly, leaving only new trailing field(s) at 0.
  `alarm_size_for_schema()` is the gate: it maps a stored schema version to
  its historical `sizeof(Alarm)` as a **frozen literal** (never `sizeof
  (Alarm)` itself, which tracks the current struct and would be wrong for
  a past version once the struct grows again); a version with no case there
  (its `default: return 0`) either predates this migration or was a
  declared hard break, and `store_load()` wipes for both of those, plus for
  a newer-than-us stored schema (a downgrade) — exactly like every schema
  mismatch did before this migration existed. On a successful migration,
  `store_load()` immediately writes the current `STORE_SCHEMA` back via
  `persist_write_int` so a later load doesn't re-check, even though the
  per-alarm blobs themselves stay physically old-sized on disk until the
  next real `store_save()` (any ordinary mutation triggers one via
  `persist_all()`). This only works because every field added to `Alarm`
  so far was appended at the very end (see the ground rules in
  `STORE_SCHEMA`'s own comment, `alarm_store.h`) — verified manually once,
  by temporarily growing `Alarm` by a field and bumping `STORE_SCHEMA` in a
  throwaway build installed over an emulator instance with existing schema-7
  data, confirming the alarm survived instead of being wiped (see
  `TODO.md`).
- **A field-append migration isn't always a free pass, though**: adding
  `cron_dom_mask`/`cron_month_mask` (schema 9, cron mode's day-of-month/month
  fields — see "Cron-syntax alarms" below) is the first bump where the
  zero-fill-on-migration default is actively WRONG, not just unused. Every
  earlier append (`vibe_pattern`, `increasing_volume`) happened to have 0 as
  its correct default; a zero-filled bitmask instead means "matches
  nothing", which for an existing cron alarm would silently and permanently
  stop its day/month matching the instant this shipped. `store_load()`
  handles this with an explicit fixup, not just a size-table entry: for any
  alarm loaded from `stored_schema < 9` where `is_cron` is true, it
  overwrites `cron_dom_mask`/`cron_month_mask` to `AC_DOM_ALL`/`AC_MONTH_ALL`
  (and the raw `cron_dom`/`cron_month` text to `"*"`) right after the
  `persist_read_data` copy. Schema 10 (`cron_week_mask`, cron mode's
  ISO-week field) needed the identical fixup, gated on its own
  `stored_schema < 10` check — worth checking for on every FUTURE append
  too, the "zero is a safe default" assumption doesn't automatically hold
  just because the previous several bumps got lucky.
- **Never copy a `MAX_ALARMS`-sized array of `Alarm` structs onto the
  stack** — a real crash, not a theoretical one: growing `Alarm` to 248
  bytes (from 184) for the schema-9 cron dom/month fields made
  `compute_next_fire_time()`'s old `Alarm scan[MAX_ALARMS]` local (a
  filtered copy it built just to hand to `ac_next_occurrence()`) balloon to
  ~4KB of stack, several call-frames deep from `init()`
  (`init()` → `rearm_wakeup()` → `compute_next_fire_time()`). Installing a
  build with this change over any existing installation that had a real
  alarm saved crashed immediately on launch — `App fault! PC: 0 LR: 0`, the
  classic signature of a stack overflow corrupting a return address, not a
  logic bug in the new cron math itself (confirmed by reproducing it in the
  emulator: build the pre-change version, seed alarms via test hooks,
  install the new build **over** that data without wiping — the exact
  upgrade scenario — and it fails the same way; a fresh/wiped install never
  hit it, since `s_count == 0` skips the array entirely). Fixed by scanning
  `s_alarms[i]` directly instead of copying into a filtered local array,
  the same style the cron loop right below it already used — no behavior
  change, just removes the copy. The lesson generalizes: any local
  `Alarm[MAX_ALARMS]`-shaped variable is a stack-budget risk that gets
  worse every time `Alarm` grows, and won't show up in `pebble build`, only
  by actually installing over real persisted data.
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
- **`repeats` (bool) is derived from `repeat_days` (bitmask), not set
  directly**: `repeats` says whether the alarm recurs weekly forever or
  fires once; `repeat_days` says which day(s) apply. The repeat editor has
  no separate on/off control — `repeat_edit_window_push`'s Apply always
  computes `repeats = (repeat_days != 0)` from whatever weekdays ended up
  picked (`repeat_confirm_and_pop`/`repeat_back`, `main.c`). This means a
  *non-repeating* alarm can never target one specific weekday through this
  editor (`repeats=false` with one bit set in `repeat_days`, e.g. "fire
  once, next Friday") — a capability the app supported earlier but
  deliberately dropped when the editor was rebuilt (see "Repeat editor" key
  design point below); a `repeat_days==0` alarm now always means "next
  occurrence of the time, today/tomorrow", with no specific day. Existing
  persisted alarms in that now-unreachable state (`repeats=false`, exactly
  one `repeat_days` bit set) are left as-is by every other code path — only
  actually opening this alarm's Repeat screen and pressing Apply collapses
  it into the derived model. `ac_next_offset_days` scans `repeat_days` the
  same way regardless of `repeats`; only `ac_mark_fired` (disable vs. keep
  recurring) and `skip_next` (repeats-only) branch on it.
- **Ring screen click config is intentionally asymmetric**: DOWN needs
  `window_multi_click_subscribe` (double-click to stop) while UP is a plain
  single-click (snooze). BACK is explicitly subscribed to a no-op handler —
  leaving it unbound would let Pebble's default "pop the window" behavior
  silently dismiss the alarm, which is the opposite of the intended
  "BACK does nothing, ringing continues" behavior.
- **No touch-dial reuse**: time/duration entry uses plain button-driven
  pickers (UP/DOWN adjust, SELECT advance/confirm, BACK retreat/cancel),
  not a port of the vendored `touch_dial` widget.
- **Time/snooze editors both follow the same SELECT-advances/BACK-retreats
  pattern**, each a plain custom `Layer` (not a `MenuLayer` — the repeat and
  cron editors are `MenuLayer`s instead, see below): SELECT moves forward
  one field/box, confirming (submitting) when already on the last one; BACK
  moves back one field/box, **cancelling** once already on the first one.
  What cancelling does depends on the caller-supplied `on_cancel` (a 4th
  param on `time_edit_window_push`/`snooze_edit_window_push`, alongside the
  existing `on_confirm`): if non-NULL, it's called instead of `on_confirm`
  — used by the "+ New alarm" wizard (`new_alarm_wizard_cancel`) so exiting
  any of its screens aborts the whole flow outright, discarding the draft;
  the alarm is only actually created by completing the last screen (the
  label) via SELECT. The wizard has no Snooze screen any more — it used to,
  but `new_alarm_repeat_confirm`/`new_alarm_cron_confirm` now go straight
  from Repeat/Cron to the label keyboard, since `s_draft.snooze_minutes`/
  `snooze_max` are already seeded from the phone-configured defaults in
  `start_new_alarm_flow`; every new alarm just uses those outright. If `on_cancel` is NULL — every per-alarm edit-menu call
  site (`edit_on_*_confirm`) — cancelling instead reverts to the value the
  screen was opened with and still calls `on_confirm` with that original
  value, so editing a single field of an already-existing alarm behaves as
  a no-op on cancel rather than needing its own special case. The repeat and
  cron editors follow this same on_cancel-vs-snapshot-revert convention on
  BACK, just via a `MenuLayer`'s vertical selection instead of a horizontal
  field cursor (see below).
- **Repeat editor is an itemized `MenuLayer`**, the same shape as the cron
  editor: row 0 is "Apply", rows 1-7 are the individual weekdays (full
  names, e.g. "Monday") starting at the configured first-day-of-week, each
  an independent checkbox row toggled by SELECT (`repeat_select`,
  `repeat_draw_row`) — a `"[X] "`/`"[  ] "` marker precedes the day name in
  one left-aligned string, rather than a separate right-aligned "On"/"Off"
  value, so the checked state reads at the same glance as the label. There
  is no separate on/off item — `repeats` isn't set directly here at all,
  it's derived on Apply from whether any weekday ended up picked (see the
  `repeats`/`repeat_days` key design point above).
  UP/DOWN are `single_repeating_click`, so holding them scrolls
  continuously; long-press SELECT submits outright regardless of cursor
  position; long-press BACK is an explicit no-op. Because the window (and
  its `MenuLayer`) is created once and reused across every call site,
  `repeat_edit_window_push` explicitly resets the selection to row 0
  ("Apply") on every open — otherwise a prior call's scroll position would
  leak into the next one, the same fix applied to the cron editor.
- **Main list SELECT is split short-press/long-press** (`ml_select`/
  `ml_select_long`): short-press cycles the alarm's state directly, with no
  confirm prompt in between (`ml_cycle_alarm_state`) — a repeating alarm
  advances enabled (normal) -> disabled -> enabled-with-skip-next-pending ->
  back to enabled (normal), always the same direction regardless of which
  state it's currently in; a non-repeating alarm has no skip state (`skip_next`
  is meaningless for a one-time alarm) and just toggles enabled/disabled. This
  used to prompt "Disable" vs "Skip next occurrence" for a repeating, enabled
  alarm (via `confirm_window_push`) rather than disabling outright, to guard
  against an accidental press silently cancelling every future occurrence;
  the cycle replaces that guard with reversibility instead — every state is
  one more short-press away from undoing itself, so there's no destructive
  step to guard. The edit menu's own State row (`edit_select`'s
  `EDIT_ROW_ENABLE` case) now shares this exact cycle too — it just calls
  `ml_cycle_alarm_state(s_edit_idx)` directly (`s_edit_idx` indexes
  `s_alarms` the same way the main list's row index does after resolving
  `s_order`), then reloads the edit `MenuLayer` itself since `reload_ui()`
  doesn't know about it. This used to be a separate confirm-prompt version
  (`edit_on_enable_choice`, now removed) that asked "Disable" vs "Skip next
  occurrence" — replaced for the same reversibility reason the main list's
  own confirm prompt was dropped, and so the two entry points no longer
  diverge in behavior. Long-press opens the full edit menu
  (`open_edit_window`), which every other field lives behind now. The "+ New
  alarm" row only responds to short-press (starts the wizard); long-press on
  it is an explicit no-op, not "open some nonexistent edit menu for it".
- **`skip_next` toggles both ways from either entry point**: both the main
  list's cycle and the edit menu's State row now go through the same
  `ml_cycle_alarm_state` — it sets `skip_next` on the disabled ->
  enabled-with-skip transition and clears it on the enabled-with-skip ->
  enabled (normal) transition. The edit menu's State row surfaces the
  pending state too: it reads "Skip next" (not "Enabled") whenever
  `enabled && (repeats || is_cron) && skip_next`, so it isn't otherwise
  invisible until its occurrence silently doesn't ring.
- **Visual style matches `pebble-another-timer`**, ported deliberately:
  - The main list (`ml_row_colors`/`ml_draw_alarm_row`) mirrors timer's
    `ml_row_colors`/`ml_draw_row` — per-row state tinting (snoozing=red,
    enabled=green, skip-pending=yellow, disabled=white; selected rows get a
    darker/black variant of the same color), a fixed-width bold time
    followed by a lighter-weight label, and a divider line under each row.
    skip-pending (`enabled && repeats && skip_next`) reuses timer's exact
    `TS_PAUSED` colors verbatim — `GColorYellow`/black unselected,
    `GColorArmyGreen`/white selected (`ml_row_colors` takes a `skip_pending`
    bool alongside `enabled`/`snoozing`, checked ahead of `enabled` so a
    skip-pending row doesn't also match the plain-enabled branch; `snoozing`
    still outranks it, since only an enabled alarm can be snoozing or
    skip-pending in the first place). Row order is by raw clock time
    (hour:minute) ascending, ties broken by index — not time-to-next-
    occurrence, so a snoozed, skip-pending, or disabled alarm still sorts by
    its own hour:minute like any other. Each row's first line also gets a
    leading play/stop/skip icon (`ml_draw_state_icon`, `MlIcon` enum) — play
    and stop are a direct port of timer's own `ml_draw_state_icon`'s
    `TS_RUNNING`/`TS_STOPPED` cases (scanline-built triangle / filled
    square), drawn in the row's existing `fg` tint; skip is a third case
    with no timer equivalent — the same filled square shape (also `fg`, so
    it reads as the same family of symbol as play/stop, not a color of its
    own), with a "1" digit cut into it in the row's `bg` tint for contrast
    (one specific occurrence being skipped, not the alarm stopped outright).
    Because drawing that digit sets the graphics context's text color to
    `bg`, `ml_draw_alarm_row` explicitly resets it back to `fg` right after
    the icon call, before drawing the rest of the row's text — otherwise the
    time/label/repeat-summary text on a skip-pending row would silently
    inherit `bg` too. Icon drawn with the time text shifted right 16px to
    make room; line 2 is unaffected and stays flush-left.
  - The trailing "+ New alarm" row is one row tall (`ML_NEW_ROW_H`, 34px —
    the same single-row height as the cron/repeat/confirm/edit itemized
    menus), not the taller two-line `ML_ROW_H` every alarm row uses;
    `ml_cell_height` special-cases `row == s_count` for this. `main_hint_
    update_proc`'s free-space math (`ML_ROW_H * s_count + ML_NEW_ROW_H`) has
    to add these two different row heights rather than multiply by a single
    uniform one, unlike timer's `empty_hint_update_proc` this was adapted
    from — get this wrong and the hint text is off-center or clipped, since
    it's positioned relative to where the list rows actually end, not a
    fixed offset.
  - A one-alarm hint (`main_hint_update_proc`) is a direct adaptation of
    timer's `empty_hint_update_proc` (its `s_count == 1` branch): an overlay
    `Layer` (`s_main_hint_layer`) the same size as the `MenuLayer`, added on
    top of it, that draws "- Short-press to cycle state\n- Long-press to
    edit" centered in the blank space below the list — only when there's
    exactly one alarm (so exactly the alarm row + the trailing "+ New alarm"
    row). Kept in sync via a single `layer_mark_dirty` call added to
    `reload_ui()`, since every alarm-count/state change already funnels
    through it.
  - The main window has a bottom bar (`draw_bottom_bar`/
    `bottom_bar_rect_for_bounds`), a direct port of timer's own bottom bar:
    a thin divider line + black strip pinned to the bottom, reducing the
    content above it by `BOTTOM_BAR_H` rather than overlaying it. Current
    time (system 12h/24h-aware `clock_copy_time_string`) on the left; the
    next scheduled alarm's due time on the right via `compute_next_fire_time`
    and the shared `format_relative_fire_time()` helper (`main.c`) — "Next:
    --" if nothing is scheduled, otherwise a 3-tier format depending on how
    far out the next fire time is: today shows time only ("Next: 07:00", no
    weekday or date — redundant otherwise); within the next week (1-7 days
    out) shows a weekday abbreviation + time ("Next: Sat 06:00"); further out
    shows date only, no time ("Next: 1.6." — neither day nor month
    zero-padded) — this last tier only matters
    for cron alarms (a legacy alarm's own scheduling model can never put its
    next occurrence more than a week out), but the rule is unconditional so
    the two code paths sharing this formatter never disagree. Day-granularity
    is computed via `day_diff()` (zeroing both instants to midnight and
    `mktime()`-diffing them, not dividing the raw epoch difference by 86400,
    which breaks across DST transitions). Refreshed once a minute via
    `tick_timer_service_subscribe` (`handle_minute_tick`), guarded per-window
    on that window actually being the top of the window stack.
  - **The bottom bar isn't main-window-only**: `bottom_bar_attach()` (takes a
    window's root `Layer`, creates/adds the bar child, returns it) and
    `bottom_bar_top_for_bounds()` are shared, forward-declared near the top
    of `main.c` so the window-load functions defined earlier in the file
    (time/cron editors, the alarm edit menu) can call them before the bar's
    own implementation appears later in the file. Each of those three
    windows shrinks its own content (the custom time-editor `Layer`, or a
    `MenuLayer` for cron/edit) to `bottom_bar_top_for_bounds()` (minus its
    own header height, for the cron editor) and calls `bottom_bar_attach()`
    in `_window_load`, destroying the returned layer in `_window_unload` —
    same shape as the main window's own `s_bottom_bar_layer`, just one
    static `Layer*` per window (`s_time_bottom_bar`/`s_cron_bottom_bar`/
    `s_edit_bottom_bar`). `handle_minute_tick` checks all four against
    `window_stack_get_top_window()` individually rather than a single
    shared flag, since exactly one of them (if any) is on top at a time.
    The "+ New alarm" wizard reuses the same cached time/cron windows as
    the per-alarm editors, so it gets the bar for free there with no
    separate wiring. The **repeat editor deliberately does NOT get one** —
    it had it briefly, but it was removed on request; `repeat_window_load`
    just sizes its `MenuLayer` to the window's full height again, with no
    `bottom_bar_attach()` call and no `s_repeat_bottom_bar` variable. The
    cron *help* screen and the generic 2-choice confirm window were never
    given one either — neither was asked for, and the help screen's
    scrollable text content makes a fixed-height reduction less obviously
    correct there.
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
legacy mode: `is_cron` alarms are defined by six independent cron-style
fields — minute (0-59), hour (0-23), day-of-month (1-31), month (1-12),
day-of-week (0-6), ISO 8601 week-of-year (1-53) — each written as `*`, `N`,
`N-M`, `*/N`, `N-M/N2`, or a comma-list of these (e.g. `"*/20"`, `"9-17/2"`,
`"1,5,10-15"`), parsed by `ac_cron_parse_field` into a bitmask. Day-of-month
and month were a later addition on top of the original 3-field
(minute/hour/day-of-week) design — adding them required real calendar-date
math (month lengths, leap years), which is why `alarm_calc.c` gained the
small self-contained `ac_is_leap_year`/`ac_days_in_month`/`ac_day_of_week`
(Sakamoto's algorithm)/`ac_advance_calendar_day` helpers below, all still
pure integer math with no `<time.h>` dependency, preserving the
host-testable-core design. Week-of-year was a further addition on top of
that: `ac_day_of_year`/`ac_iso_weekday`/`ac_iso_year_has_53_weeks`/
`ac_iso_week_number` implement the ISO week calculation, the single trickiest
piece of pure math in this file — week boundaries don't align with calendar
year boundaries (the first few days of January can belong to the LAST week
of the previous year, and the last few days of December can belong to week 1
of the FOLLOWING year), both handled by the standard ISO formula plus its
two edge-case corrections. Verified against Python's `datetime.isocalendar()`
(the reference implementation) for both boundary directions before being
wired into the rest of the engine.

**Every combination of the six fields is allowed** — there is no rejection
of "conflicting" or rare/impossible combinations at all, a deliberate design
choice (and a reversal of an earlier, more restrictive version of this
feature — see git history if curious):

- **Day-of-month and day-of-week can both be a real restriction on the same
  alarm, and they're ANDed together, not OR'd**: real cron's traditional
  rule when both are restricted is to OR them ("fire on the 1st OR any
  Monday"); this app always ANDs instead, which is what makes "day 1-7 AND
  weekday Monday" (first Monday of every month) expressible at all —
  `ac_cron_is_due`/`ac_cron_first_match_after` just AND `cron_dom_mask` and
  `repeat_days` together like every other field, with no special case for
  "both restricted." Implementing real cron's OR rule was deliberately
  rejected in favor of this simpler, more useful (for this app's purposes)
  semantics.
- **Week-of-year is ANDed in the same unconditional way** — `cron_week_mask`
  joins `dom_mask`/`month_mask`/`dow_mask`/`hour_mask`/`min_mask` as one more
  flat AND term in `ac_cron_first_match_after`'s day-matching check, with no
  special case. This is what makes biweekly alarms expressible: `"*/2"` in
  week (odd ISO weeks) combined with a specific weekday and time gives "every
  other Monday at 07:00", something neither the legacy weekly-repeat model
  nor the original 3-field cron could express at all.
- **Rare or literally impossible combinations (day 29 + February, day 31 +
  April, week 53 in a year that doesn't have one) are accepted too** —
  there's no validation rejecting them, because
  `ac_cron_next_offset_days`'s bounded forward search
  (`CRON_DAY_SCAN_MAX` = 400 days, `alarm_calc.c`, deliberately sized to
  match `wakeup_schedule()`'s own ~1-year scheduling horizon rather than
  "large enough to always eventually find a match") already has a
  well-defined answer for "no match within the horizon that matters": it
  returns -1, same as it would for a genuinely malformed mask. The cron
  editor surfaces this as a live "(Next: ...)" preview on its Apply row (see
  `cron_apply_preview()`, `main.c`) instead of a validity error — a rare
  pattern shows its real (possibly months/years-away) next date when one
  exists within the search bound, formatted via the same
  `format_relative_fire_time()` 3-tier rule the bottom bar's own "Next: ..."
  uses (today: time only; within a week: weekday + time; further out: date
  only — see the bottom bar design point above), or "(Next: never/1y)" when
  no match exists within the bound, letting the user judge a pattern by its
  actual computed effect rather than an abstract rule about which fields may
  be combined. This preview is cron-editor-only — the main list and edit
  menu's Cron row summary both still just show the raw configured fields
  (`ac_format_cron_summary`), unchanged.

- **True multi-fire semantics, a deliberate departure from every other
  alarm in the app**: a cron alarm can ring many times a day if its pattern
  matches many times (e.g. `*/20` rings every 20 minutes). There is
  **no suppression window** after stopping/snoozing a firing — the very
  next matching minute fires again. This is intentional, not a bug to later
  "fix" (see the comment at `sweep_due_alarms()`'s cron branch, `main.c`).
- **Day-of-week reuses `repeat_days`/`AC_DAY_BIT`** — no separate field:
  when `is_cron`, `repeat_days` holds the parsed cron dow mask instead of
  the legacy weekly-repeat days, and `repeats` is unused (forced
  false/ignored). Every legacy code path touching that field must branch on
  `is_cron` — missing a guard silently corrupts a cron alarm's display or
  scheduling. The one non-obvious spot: both the edit menu's ENABLE-row
  re-enable handler (`edit_select`'s `EDIT_ROW_ENABLE` case) and the main
  list's short-press state cycle (`ml_cycle_alarm_state`) used to call
  `resync_last_fired_for_schedule_change()` (which reads `repeat_days` under
  legacy semantics) unconditionally on re-enable — both now branch on
  `is_cron` first (`a->cron_last_fired_min = -1` instead). The main-list
  cycle's version of this was a real latent bug (not just theoretical): any
  cron alarm re-enabled from the main list would silently take the wrong
  path, until `skip_next` support (below) moved cron alarms into the same
  three-state cycle branches as repeating legacy alarms and this got fixed
  alongside it.
- **`skip_next` is reused (not repurposed) for cron alarms**, unlike
  `repeat_days`: same "skip just the next occurrence" meaning as legacy,
  just applied to cron's minute-granularity schedule instead of a whole day.
  `ac_cron_next_offset_days()` (`alarm_calc.c`) takes a `skip_next` param and
  reports the SECOND matching (day-offset, hour, minute) instead of the
  first when set — mirroring `ac_next_offset_days`'s own skip handling,
  factored through a shared `ac_cron_first_match_after()` scan helper. That
  scan walks real calendar days (`now_year`/`now_month`/`now_day` in, up to
  `CRON_DAY_SCAN_MAX` days ahead) rather than either a bare 7-day weekday
  wraparound (the original 3-field design) or a 3-dimensional day/hour/minute
  loop with no calendar awareness — `month_mask`/`cron_dom_mask` restrict
  which calendar dates match, so the walk needs `ac_days_in_month`/
  `ac_is_leap_year` to advance correctly across month and year boundaries,
  and `ac_day_of_week` to still evaluate `dow_mask` per candidate date.
  `ac_cron_is_due()` itself stays skip-unaware, exactly
  like legacy's `ac_is_due()` — the skip is enforced entirely by
  `rearm_wakeup()` (via `compute_next_fire_time()`) never scheduling a
  wakeup for the skipped minute in the first place, which is sufficient
  because `sweep_due_alarms()` is ONLY ever invoked from `init()` or a real
  `WakeupEvent` (`handle_wakeup_event`) — there's no periodic re-check that
  could otherwise re-evaluate a due cron minute independent of wakeup
  timing. `sweep_due_alarms()`'s cron branch clears `skip_next` once the
  resumed (non-skipped) occurrence actually rings, mirroring
  `ac_mark_fired()`'s legacy skip_next consumption. Surfaced identically to
  legacy in every UI spot that previously gated on `a->repeats`: the main
  list's skip icon/tint, the edit menu's "Skip next" state text, the edit
  menu's Disable/"Skip next occurrence" confirm prompt, and the main list's
  short-press three-state cycle all now check `a->repeats || a->is_cron`.
- **No eager-fire guard needed on create/edit**, unlike legacy's
  `resync_last_fired_for_schedule_change`: for cron, "the pattern currently
  matches right now" **is** the real, immediate next occurrence, so every
  cron create/edit path just sets `cron_last_fired_min = -1`. `main.c`'s
  `sweep_due_alarms()` dedups on exact epoch-minute
  (`ac_cron_is_due`/`cron_last_fired_min`) rather than `last_fired_day`'s
  whole-day dedup, since a cron alarm can genuinely fire many times a day.
- **Secret long-press SELECT on the time editor** switches it to cron entry,
  regardless of which field (hour/minute) is currently focused
  (`time_select_long`, `window_long_click_subscribe(BUTTON_ID_SELECT, 500,
  ...)` alongside the time editor's existing plain `window_single_click_
  subscribe` on SELECT — both coexist on the same button, same pattern the
  cron/repeat editors' own Apply row uses). `time_edit_window_push` gained a
  5th `on_chord` callback parameter for this. A hint ("Long-press select\nto
  enter cron mode", `GOTHIC_24`, horizontally centered) is drawn in whatever
  room is left below the hour/minute boxes whenever `on_chord` is non-NULL.
  The cron fields default to everyday/every-day-of-month/every-month
  (`dow = dom = month = "*"`) in both cases, but the hour/minute default
  differs by call site: converting an already-existing alarm
  (`edit_on_time_chord`) keeps the exact currently-staged hour/minute (e.g.
  `"30 20 * * *"` for 20:30, read directly off `s_time_hour`/`s_time_minute`,
  still valid at the moment `on_chord` runs since popping the time window
  doesn't reset them) — editing an existing schedule shouldn't silently
  change the time itself. The "+ New alarm" wizard (`new_alarm_time_chord`)
  instead defaults minute to `"0"` and hour to the *next* full hour
  (`s_time_hour + 1` if `s_time_minute > 0`, else unchanged) — a brand new
  cron alarm is far more likely to want "top of the hour" than whatever
  incidental minute the time editor happened to be showing.
- **One `cron_edit_window_push()` window serves every call site** — the "+
  New alarm" wizard (the long-press replaces the Time+Repeat steps, skipping
  straight to Snooze), converting an existing normal alarm (long-press on
  its edit menu's Time row — `edit_cron_convert_cancel` is a real no-op
  cancel, not `NULL`, since there's no prior cron state to revert to), and
  re-editing an existing cron alarm (its edit menu's Cron row — real `NULL`
  snapshot-revert cancel semantics apply here). `MenuLayer`-based: row 0
  "Apply", rows 1-6 the six fields in display order Minute, Hour, Day,
  Month, Weekday, Week — `CRON_ROW_MINUTE`/`CRON_ROW_HOUR`/`CRON_ROW_DAY`/
  `CRON_ROW_MONTH`/`CRON_ROW_DOW`/`CRON_ROW_WEEK` assigned row numbers 1-6 in
  that order, matching the `min_str, hour_str, dom_str, month_str, dow_str,
  week_str` parameter order every function signature in this file uses
  (`cron_edit_window_push`, `ac_format_cron_summary`, `ac_cron_*`) — row
  order and parameter order are the same convention everywhere, deliberately
  kept in sync rather than letting the on-watch display order drift from the
  code's own field order. Weekday comes before Week (not the field-addition
  order they were introduced in) since it reads more naturally that way on
  the watch. Each row opens the shared multitap keyboard to
  edit its raw text; live "(invalid)" annotation if `ac_cron_parse_field`
  currently fails for that field — never blocks typing, only Apply
  (`cron_submit()`) is gated on every field parsing cleanly (there's no
  cross-field validity check any more — see above). The Apply row itself
  (`CRON_ROW_SUBMIT`) draws a live "(Next: ...)" preview
  (`cron_apply_preview()`) alongside the "Apply" label,
  same key/value layout as every other row, computed from the currently-
  staged fields against real "now" — blank if any field is currently
  unparseable (a preview isn't meaningful for invalid syntax, and the
  field's own "(invalid)" tag already covers that case). BACK follows the
  same on_cancel-vs-snapshot-revert convention as the time/repeat/snooze
  editors. Long-press SELECT submits outright regardless of cursor
  position; long-press BACK is a no-op.
- **Edit menu display**: `edit_build_rows()` builds the ordered row-kind
  list for the current alarm (collapsing Time+Repeat into one `EDIT_ROW_CRON`
  row when `is_cron`) so `edit_num_rows`/`edit_draw_row`/`edit_select` share
  one source of truth for "which row is at this menu position" without
  scattering `is_cron` checks through each of them individually. Row order:
  Delete, Time (or Cron), Label, State, Repeat (only when `!is_cron`),
  Snooze, Vibration, Vibe pattern, Sound, Increasing volume, Auto-stop —
  Delete leads (so it's the row selected by default when the menu opens;
  still gated behind its own confirm prompt, so landing on it isn't
  destructive by itself), and Time/Cron comes next ahead of Label/State,
  unlike every other row grouping in this app which puts Label first.
- **Main list display**: the bold time slot shows "Cron" instead of a
  formatted time; the summary line below shows all six raw cron fields
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
  math incl. multi-fire progression across successive calls, month/day-of-
  month-restricted scans crossing a month boundary, the day-of-month-AND-
  day-of-week "first Monday of every month" case, a rare leap-day pattern
  legitimately reporting no match within the bounded search, a biweekly
  (week `*/2`) progression, a rare week-53 pattern legitimately reporting no
  match, the January-belongs-to-the-previous-ISO-year boundary case,
  due-check incl. the older-`last_fired_min`-still-due multi-fire case, and
  summary formatting). All calendar-dependent tests share one fixed
  reference date (`REF_YEAR`/`REF_MONTH`/`REF_DAY` = 2024-01-03, a
  Wednesday) so day-offset assertions stay easy to hand-verify.
- `tests/dict.test.js` — Node's built-in `node:test` + `node:assert`, run
  against **compiled** `src/pkjs/dict.js` (not `src/ts` directly), which is
  why `npm test` has a `pretest: tsc` step.
