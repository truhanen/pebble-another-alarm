# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Pebble docs

When looking up Pebble SDK API/guide documentation, always use
`developer.repebble.com` — never `developer.rebble.io`.

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

**Don't leave stale/overlapping `pebble build` (or `make build_and_install_*`)
processes running.** waf serializes all builds behind one global lock file
(`.lock-waf_darwin_build`) — a second build invoked while an earlier one (or
a backgrounded `pebble build || pebble build` retry chain) is still alive
doesn't run in parallel, it just queues behind the lock, which is what makes
an unrelated later build look mysteriously slow or hung. Before kicking off
a build, check for one already in flight (`ps aux | grep -i "pebble build\|waf"`)
and let it finish or kill it rather than layering another on top; don't launch
a build in the background and then forget about it.

## Emulator testing (headless)

Before running `pebble` commands against the emulator, consult the
`pebble-emulator` skill for general headless-emulator practices
(`--vnc` consistency, kill+wipe hygiene, concurrent `pebble logs`,
screenshot-based navigation). This app only targets `--emulator emery`.

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
| `TestClearAlarms` | 10008 | int, nonzero | Wipes all alarms (`s_count = 0`) before anything else in the same message. |
| `TestAlarmIndex` | 10009 | int | **Required** to touch alarm fields. `== s_count` appends a new alarm (seeded with the same defaults as the "+ New alarm" wizard — enabled, vibration+sound on, Double vibe pattern, default snooze, one-time — except the time, which is a fixed 7:00 here rather than "next minute", for a deterministic default unless overridden); `< s_count` edits that existing alarm in place; anything else is ignored (logged as a warning). |
| `TestAlarmHour` | 10010 | int 0-23 | |
| `TestAlarmMinute` | 10011 | int 0-59 | |
| `TestAlarmRepeats` | 10012 | int 0/1 | |
| `TestAlarmRepeatDays` | 10013 | int 0-127 | `AC_DAY_*` bitmask. |
| `TestAlarmEnabled` | 10014 | int 0/1 | |
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
| `TestAlarmAutoDismiss` | 10029 | int 0-2 | Sets `auto_dismiss_mode` directly (0=Off, 1=On, 2=On+hide — see "Key design points" below). |
| `TestAlarmVibePattern` | 10030 | int 0-2 | 0=Double, 1=Short, 2=Long. Sets the per-alarm `vibe_pattern` (see "Key design points" below); defaults to 0/Double on a newly appended alarm regardless of the phone-configured default, same deterministic-defaults rationale as `TestAlarmIndex`'s other hardcoded fields. |
| `TestAlarmIncreasingVolume` | 10031 | int 0/1 | Sets `increasing_volume` directly (see "Key design points" below). |
| `TestAlarmCronDom` | 10032 | string | Raw cron day-of-month field (range 1-31), parsed into `cron_dom_mask`. See "Cron-syntax alarms" below. |
| `TestAlarmCronMonth` | 10033 | string | Raw cron month field (range 1-12), parsed into `cron_month_mask`. See "Cron-syntax alarms" below. |
| `TestAlarmCronWeek` | 10035 | string | Raw cron ISO week-of-year field (range 1-53), parsed into `cron_week_mask`. See "Cron-syntax alarms" below. |
| `TestAlarmSoundPatternKind` | 10036 | int 0/2 | 0=Preset (use `sound_preset`), 2=Library (use `custom_tune_id`) — 1 ("inline Custom") is retired, see "Signal submenu / custom sound &amp; vibe patterns" below. Sets `sound_pattern_kind` directly; no test hook sets `sound_preset` itself, since main.c's own Tune picker already exercises that path directly. |
| `TestAlarmVibePatternKind` | 10038 | int 0-2 | 0=Preset (use `vibe_pattern`), 1=inline bytes (what a picked Double/Short/Long preset is stored as), 2=Library (use `custom_vibe_id`). Sets `vibe_pattern_kind` directly. |
| `TestAlarmVibeCustomPattern` | 10039 | string | Space-separated duration/rest pattern in the current ABC-notation grammar (e.g. `"3 z 1"` — `z` is a rest; see "Custom sound/vibe patterns are a genuine ABC-notation subset" below), parsed via `ac_vibe_pattern_parse` into `vibe_pattern_custom`/`vibe_pattern_len`. Logs a warning (field left unchanged) if unparseable — notably, the OLD pre-`STORE_SCHEMA`-20 digit+dash grammar (e.g. `"2 1- 2"`) no longer parses under this one. |
| `TestClearTunes` | 10040 | int, nonzero | Wipes the entire custom-tune library (`s_tune_count = 0`), independent of `TestClearAlarms`. See "Custom-tune library" below. |
| `TestTuneIndex` | 10041 | int | **Required** to touch tune fields, mirroring `TestAlarmIndex`'s own convention: `== s_tune_count` appends a new (empty-named) tune with a freshly allocated id; `< s_tune_count` edits that existing tune in place. |
| `TestTuneName` | 10042 | string | |
| `TestTuneCustomPattern` | 10043 | string | Space-separated note/rest pattern in the current ABC-notation grammar (e.g. `"C z E z G"` — `z` is a rest; see "Custom sound/vibe patterns are a genuine ABC-notation subset" below), parsed via `ac_sound_pattern_parse` into the tune's own `pattern`/`pattern_note`/`pattern_len` (`CustomTune`, unaffected by sound's own `kind==1` retirement — the tune library was always a separate representation). Logs a warning (left unchanged) if unparseable (the OLD pre-`STORE_SCHEMA`-20 note-then-duration-digit grammar, e.g. `"1C 2D"`, no longer parses under this one). `10037` (`TestAlarmSoundCustomPattern`) used to be this same grammar's alarm-level equivalent, setting `Alarm.sound_pattern`/`sound_pattern_note`/`sound_pattern_len` directly — removed along with those fields; the key id is still declared (harmless, an unused integer) but has no handler any more. |
| `TestClearVibes` | 10048 | int, nonzero | Wipes the custom-vibration library (`s_vibe_count = 0`). See "Custom-vibration library" below. |
| `TestVibeIndex` | 10049 | int | **Required** to touch vibration fields, same convention as `TestTuneIndex`: `== s_vibe_count` appends a new (empty-named) vibration with a fresh id, `< s_vibe_count` edits that one. |
| `TestVibeName` | 10050 | string | |
| `TestVibePattern` | 10051 | string | Space-separated vibration pattern (e.g. `"1 z/2 1"`), parsed via `ac_vibe_pattern_parse`; unparseable = warning, left unchanged. |
| `TestAlarmVibeId` | 10052 | int | Links an alarm to a vibration-library entry by id — sets `custom_vibe_id` and forces `vibe_pattern_kind = 2`. (`VibeConfig`, 10047, is the production key for the whole library.) |
| `TestAlarmTuneId` | 10044 | int | Links an alarm directly to a tune by id — sets `custom_tune_id` and forces `sound_pattern_kind = 2`, skipping the Tune picker → Custom tunes → Custom tune → Apply chain for test setup. |

Only keys present in the message are applied — anything omitted is left
untouched on an existing alarm (or defaulted, per above, on a new one).
Applying any field calls `persist_all(); rearm_wakeup(); reload_ui();`, same
as every other alarm-mutating code path. The `Test*Tune*` keys are one
exception — they call `persist_tunes()` (not `persist_all()`), since the
tune library is a separate persisted table from `s_alarms`; see "Custom-tune
library" below.

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

- **`config_clay.ts`** — five accordion sections, in order: **New alarm
  defaults** — one `alarmDefaults` component (`config_alarm_defaults.ts`)
  laid out like an alarm card's signal/snooze block (chips for Vibration/
  Sound/Increasing, aligned Vibration-preset/"Snooze (min)"/"Max snoozes"
  grid, "snooze off"/"unlimited" notes); its value is
  `{snoozeMinutes, snoozeMax, vibration, sound, increasing, vibePattern}` and
  `dict.ts`'s `flattenAlarmDefaults()` turns it into the watch's original flat
  keys (`DefaultSnoozeMinutes` — 0 means snooze off, which replaced the old
  separate "Snooze enabled" toggle — `DefaultSnoozeMax`, `DefaultSoundEnabled`,
  `DefaultVibrationEnabled`, `DefaultIncreasingVolume`, `AlarmVibePattern`),
  and `migrateSavedSettings()` seeds it from those flat keys for users who
  saved before it existed. Everything here only pre-fills a NEWLY created alarm
  (on the watch, and for an alarm added on the config page) and never touches
  existing ones; **Alarms**, **Tunes**, **Vibration patterns** (below); and
  **Other** (last) — `FirstDayOfWeek` (radiogroup, Sunday/Monday), `DateFormat` (select,
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
  built dict on save. The config page has four accordion sections
  (`config_clay_custom.ts`, ported from pebble-aika-watchface; only one open
  at a time, all closed on load; every top-level section must start with
  exactly one `heading`): **New alarm defaults** (first), **Alarms**,
  **Tunes**, and **Other** (last; the live global settings). Sections carry
  no extra bold header rows. **Alarms** (`alarmList` component,
  `config_alarm_list.ts`: per-alarm Basic/Cron toggle, cron = ONE text field
  of 6 space-separated fields, short patterns padded with `*`) and **Tunes**
  (`tuneList`, `config_tune_list.ts`, ABC patterns). Both lists use one
  RS/US-delimited CString format in both directions (`list_config.ts`, keys
  `AlarmConfig`/`TuneConfig`; it holds the authoritative, unit-tested cron/ABC
  validators — the components carry self-contained copies because Clay
  `toSource()`s them):
  - **Watch → phone snapshot**: the watch sends everything it has (tunes, then
    alarms; `build_tune_snapshot`/`build_alarm_snapshot`, `main.c`) at launch
    and, debounced 1.5s, after every `persist_all()`/`persist_tunes()`. The
    phone's `appmessage` handler stores it as `clay-settings.AlarmList`/
    `TuneList`, which is what the config page is built from — so alarms/tunes
    made on the watch show up. (Another Timer only mirrors items the watch
    sends at creation and never sends pre-existing ones; this sends all.)
    Snapshots are ignored while an unsent page edit is pending.
  - **Phone → watch push**, only after the user Saves (pending flag
    `lists_pending`, retried on `ready`; NOT on every launch like the plain
    settings, which would revert watch-side edits): `apply_alarm_config`/
    `apply_tune_config` update an existing item by id (ANY id, watch- or
    phone-made), create one only for an unknown id >= `PHONE_ID_MIN` (0x10000,
    the range the phone mints; watch ids are far below, so a stale page can't
    resurrect something deleted on the watch), and delete ONLY on a tombstone
    (a record holding just an id; the phone computes them from the ids
    present when the page opened vs. at Save, persisted until delivered).
    Nothing is deleted merely for being absent. Updates preserve runtime state
    (snooze, last-fired) and watch-only signal settings; `repeats` is only
    re-derived when the schedule actually differs (the page can't express the
    legacy "once on one weekday" state).
  - **Alarm card UI** (`config_alarm_list.ts`): each alarm is a card collapsed
    to a summary row (time · label, schedule, on/off switch; a new alarm opens
    expanded, expanded state is kept per id across re-renders). Basic/Cron is a
    segmented control, weekdays are chips in the order set by
    `FirstDayOfWeek`, and a cron alarm shows a live "Next: ..." preview
    (self-contained port of the watch's next-occurrence search, same 400-day
    horizon and today/tomorrow/weekday-within-7-days/date tiers; the authoritative
    `cronNext` lives in `list_config.ts` and is unit-tested, the browser copy is
    checked against fixed-clock cases in `tests/config_page`). Under the
    Basic/Cron selector, a cron alarm shows the watch's full cron help
    (`resources/text/cron_help.txt`, via `CRON_HELP` in `pattern_help.ts` and the
    alarmList item's `help` prop) as a collapsible `<details>`, collapsed by
    default and remembered per card across re-renders; the old one-line syntax
    reminder under the cron field is gone (the live "Next: ..." preview stays).
    Every custom list component gives its root the same 12px inset Clay's
    own components have — sides for all four (`.al-rootc`/`.tu-rootc`/`.vb-rootc`/
    `.ad-rootc` padding), plus 12px above the first item for the three lists (the
    defaults component sits under a text item that already has its own spacing):
    Clay renders a custom component's template as-is, with none of the standard
    components' padding, so without it content touches the section outline
    (a test checks every section keeps at least 8px, at the sides and below the heading).
    Cards are ordered
    like the watch's main list (`rebuild_order`: raw time of day ascending, a cron
    alarm by its earliest matching hour/minute, ties keep list order) — sorted on
    load and when a card is collapsed, never while typing, so a card doesn't jump
    away mid-edit and a new alarm stays last until collapsed. Clay's global
    `label` CSS adds padding to any `<label>`, so custom chips/switches must
    reset `padding`.
  - **Vibration patterns** section (`vibeList`, `config_vibe_list.ts`, between
    Tunes and Other): named custom vibration patterns, `VibeConfig` on the wire
    (same record shape/tombstones as tunes; sent before tunes and alarms).
  - **Built-ins and syntax help** (Tunes and Vibration patterns sections): the
    watch's built-in presets (`SOUND_PRESETS`/`VIBE_PRESETS`) are listed
    read-only above the user's own items, one line each — a plain-text label and
    a readonly value field that selects all on tap, so it can be copied with the
    system's own copy action (there is deliberately no Copy button) — and are
    never part of the saved/pushed lists. The long pattern-syntax help
    is a native `<details>` (collapsed by default) containing the watch's own help
    text. Both come from `src/ts/pattern_help.ts` via the config items'
    `builtins`/`help` props (read as `this.config` in the components, which can't
    import); `tests/pattern_help.test.js` fails if they drift from `main.c`'s
    preset tables or `resources/text/pattern_help_*.txt`.
  Known limitation: a Save from a page opened before a watch-side edit
  overwrites that item's fields with the page's older values.

AppMessage keys (`package.json`'s `pebble.messageKeys`, used as
`MESSAGE_KEY_*` in C): `FirstDayOfWeek`, `AlarmVibePattern`,
`DefaultSnoozeMinutes`, `DefaultSnoozeMax`, `AudioVolume`,
`DefaultSoundEnabled`, `DefaultVibrationEnabled`, `DateFormat` — phone→watch
only, all plain scalars, no array-type key; plus `AlarmConfig`/`TuneConfig`/`VibeConfig` (list CStrings, both directions, appended near the end: 10045/10046/10047). `DateFormat` was appended at the
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
- **Late wakeups and the catch-up window**: PebbleOS no longer refuses a
  `wakeup_schedule()` that falls near another app's wakeup — it dispatches
  close-together wakeups in turn instead, so this app's wakeup may fire a
  little *late* but is never denied. `rearm_wakeup()` therefore just calls
  `wakeup_schedule(target, 0, true)` (cookie unused); if the call is refused
  anyway (e.g. one of this app's own wakeups within a minute) the previously
  armed wakeup is left alone and a warning is logged. The OS deletes a
  wakeup's entry before dispatching it, so a late wakeup can't ask when it
  was meant to fire: `rearm_wakeup()` persists the intended minute itself
  (`PERSIST_KEY_ARMED_TARGET`, `store_load_armed_target()`/
  `store_save_armed_target()`, opaque epoch minutes, 0 = none — a top-level
  scalar like `PERSIST_KEY_WAKEUPID`, not an `Alarm` field, so no
  `STORE_SCHEMA` change). `sweep_due_alarms()` uses it as the start of a
  catch-up window running to now, capped at `WAKEUP_CATCHUP_MAX_MIN` (10).
  Legacy and snoozed alarms need nothing extra (`ac_is_due` and
  `now >= snooze_until` already catch up). A cron alarm, which only matches
  its exact minute, is checked with `ac_cron_last_match_in_range()`
  (`alarm_calc.c`, host-tested): it rings once if any minute in the window
  matches and `cron_last_fired_min` is stamped with the LAST matched minute,
  so the next-occurrence math resumes right after it. Several matches in one
  window coalesce into a single ring — a deliberate carve-out of cron's
  ring-on-every-match rule, only reachable via a late dispatch. With no
  usable armed target the window is just the current minute and the plain
  `ac_cron_is_due()` check runs. `skip_next` can't be resurrected by the
  window: the skipped match precedes the armed target (the second match).
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
  alarms, unlike `AudioVolume`. Editable per-alarm via the Signal submenu's
  "Vibration" row (see the Signal submenu design point further below) —
  it used to cycle Double -> Short -> Long -> Double directly on SELECT,
  the same short-press-cycles convention as the main list's enable/disable/
  skip state, before the Signal submenu/custom-pattern feature replaced
  that cycle with a picker (Double/Short/Long/Custom) to make room for a
  fourth, non-cyclable option. Adding this field bumped `STORE_SCHEMA` (`alarm_store.h`) to 7 — the
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
  its historical MAIN-blob `sizeof` as a **frozen literal** (never `sizeof
  (Alarm)` itself, which tracks the current struct and would be wrong for
  a past version once the struct grows again); a version with no case there
  (its `default: return 0`) predates schema 7, when this migration first
  shipped — MAIN's own layout has never changed since, so every schema from
  7 onward has an entry here regardless of what happened to the EXTENSION
  blob (see the next design point for why those are handled completely
  separately as of schema 19). On a successful migration,
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
- **A schedule now survives ANY future signal-only hard break, and a
  downgrade no longer wipes at all** (`STORE_SCHEMA` 19, `store_load()`,
  `alarm_store.c`): this was a real, repeated problem during dev testing,
  not a theoretical one — every hard break in this app's history (16→17's
  `sound_pattern[]`/`vibe_pattern_custom[]` resize, and this very bump's own
  removal of `sound_pattern_len`/`sound_pattern[]`/`sound_pattern_note[]`,
  see the Custom-tune-adjacent design point below) has only ever been about
  the EXTENSION blob's signal-pattern fields, never the MAIN blob's
  schedule fields — yet the old `store_load()` wiped the *entire* alarm
  (label, time, repeat, cron config, all of it) on any such break, because
  a single schema mismatch check gated the whole load. Worse: `cron_dom_
  mask`/`cron_month_mask`/`cron_week_mask` and their raw text are genuine
  schedule data that happen to live in the EXTENSION blob (appended there
  chronologically, not by design), so a signal-only break already put real
  schedule data at risk too. Fixed by decoupling the two blobs' own
  trust decisions entirely: `alarm_size_for_schema()` (above) now governs
  MAIN alone, and is essentially permanent (every schema 7 onward has an
  entry, since MAIN's layout has never changed). EXTENSION gets its own
  independent range check, `ext_schema_is_appendable()` (`alarm_store.c`,
  currently `stored_schema >= 19` — 19 is the schema that introduced this
  tracking, the same bump that removed `sound_pattern_len`/`sound_pattern[]`/
  `sound_pattern_note[]`, see the Custom-tune-adjacent design point below):
  when it says yes (and it's not a downgrade), EXTENSION is read in full up
  to the *current* `ALARM_EXT_SIZE` — an older-but-appendable blob simply
  doesn't have that many bytes stored yet, and `persist_read_data`'s own
  `min(buffer_size, actual_stored_size)` truncation means only however many
  bytes it actually holds get copied, leaving any newer trailing field(s) at
  their safe zero default — the same free ride MAIN's own forward migration
  has always had, so an ordinary future signal-only APPEND (a new trailing
  field, nothing resized/reordered/removed) no longer costs existing users
  their Tune/Vibration settings at all. When it says no — a genuine
  EXTENSION-layout hard break, or a *newer* stored schema (a downgrade,
  which used to wipe unconditionally via its own separate `stored_schema >
  STORE_SCHEMA` check — removed, folded into this same path) — EXTENSION
  instead reads only a frozen leading prefix, `ALARM_EXT_CORE_SIZE`
  (`alarm_store.c`): `cron_week_mask`/`cron_week`, the first two fields ever
  appended into that blob (schema 10/11), never moved since regardless of
  how many times the signal fields after them have resized. Everything from
  `sound_pattern_kind` onward resets to its safe zero default instead of
  being read at all — the same truncation property, just applied to a
  smaller requested size this time. A small sanity check on Core fields
  (`hour<24`, `minute<59`, `name` NUL-terminated, `repeat_days` in range)
  runs unconditionally on every load, cheap insurance now that a downgrade
  can hand `store_load()` bytes from a schema it's never seen. Net effect: a
  future signal-encoding rework (near-certain, given how often this exact
  area has already churned) can only ever reset "how the alarm rings," never
  "whether or when it rings" — the alarm's schedule and its own on/off state
  are completely unaffected either way — AND a future signal-only change
  that's a pure append doesn't even reset that.
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
- **`auto_dismiss_mode` is a 3-state field (Off/On/On+hide), not a bool**:
  `trigger_alarm()` branches on `a->auto_dismiss_mode` — mode 1 ("On") fires
  vibration/sound (via its own existing `vibration_enabled`/`sound_enabled`
  toggles) **exactly once** instead of calling `alarm_buzz_start()`'s
  repeating 4s cadence, shows the usual red ring screen, and arms a one-shot
  `s_alarm_auto_dismiss_timer` (`ALARM_AUTO_DISMISS_MS`, ~2s) that calls
  `alarm_do_stop()` — the same end state as the user pressing Stop — instead
  of waiting indefinitely. Mode 2 ("On, hide") fires that same one-shot
  signal but skips the ring screen (and the timer) entirely: `trigger_alarm()`
  returns early, before ever creating/pushing `s_alarm_window` or touching
  the title buffers, straight into calling `alarm_do_stop()` itself —
  reusing the exact same stop bookkeeping mode 1's timer callback uses,
  just invoked synchronously instead of after a delay. `alarm_finish_ring()`'s
  non-wakeup-launch branch is guarded with `s_alarm_window &&
  window_stack_contains_window(s_alarm_window)` specifically because of this
  — a mode-2 alarm can reach it without `s_alarm_window` ever having been
  created (e.g. the very first alarm ever triggered is mode 2), unlike every
  other caller of `alarm_finish_ring()`, which always runs after a real ring
  screen was shown. `alarm_stop`/`alarm_snooze` are refactored so
  `alarm_stop` is a thin wrapper around `alarm_do_stop()` (shared by the
  timer callback `alarm_auto_dismiss_cb` and mode 2's direct call), and
  manually pressing Stop or Snooze during mode 1's couple-second window
  cancels the pending timer first (`alarm_cancel_auto_dismiss()`, also called
  from `alarm_window_unload` as a safety net, and defensively at the top of
  every `trigger_alarm()` call so a stale timer can never fire against
  whichever alarm gets shown next) — otherwise it could fire moments later
  and clobber a snooze the user just set, or re-run the "show next pending"
  chain a second time. Cycled via the edit menu's `EDIT_ROW_AUTO_DISMISS`
  ("Auto-dismiss" Off/On/"On, hide") row, alongside Vibration/Sound; no
  wizard step, defaults to Off (like every other field not driven by a
  phone-config default). The field used to be `bool auto_dismiss` — widened
  to `uint8_t auto_dismiss_mode` in the same struct position with no
  `STORE_SCHEMA` bump, since `bool`/`uint8_t` are the same size on this
  target and the two old values (0/1) keep their exact old meaning.
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
  and an `auto_dismiss` alarm's timer-driven dismissal alike, since all
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
    darker/black variant of the same color), a fixed-width bold time on the
    left followed by a bold label right-aligned in the remaining row width,
    and a divider line under each row. The label shrinks to fit rather than
    ellipsis-truncating outright: `ml_pick_label_font()` measures the name at
    the row's normal bold size (`GOTHIC_24_BOLD`, or `GOTHIC_18_BOLD` on the
    `small` <=144px-wide layout) via `graphics_text_layout_get_content_size`
    and drops one size (`GOTHIC_18_BOLD`, or `GOTHIC_14_BOLD` when `small`)
    if it doesn't fit the space left after the time — a deliberately trimmed
    port of `multitap_keyboard.c`'s `prv_draw_text_fitted` shrink-to-fit
    ladder (single line, two font sizes, no wrapping/truncation step, since
    the row's own `GTextOverflowModeTrailingEllipsis` already handles a name
    that doesn't fit even at the smaller size). When the smaller font is
    picked, its draw position is also nudged down 6px (`ml_draw_alarm_row`) —
    empirically hand-tuned by screenshot comparison against the time text
    it shares a row with, the same "each font size needs its own vertical
    correction" issue `multitap_keyboard.c`'s own per-font `rise` offsets
    exist to fix, just applied here as a single fixed offset since there are
    only two font tiers in play rather than a whole ladder.
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
    (time/cron editors, the alarm edit menu, **and the Signal submenu** —
    easy to miss reading this file top-to-bottom, since `signal_window_load`
    sits far below this design point's own original four-window account and
    was never added to it; confirmed via every real `bottom_bar_attach(`
    call site, not just this prose) can call them before the bar's own
    implementation appears later in the file. Each of those four windows
    shrinks its own content (the custom time-editor `Layer`, or a
    `MenuLayer` for cron/edit/signal) to `bottom_bar_top_for_bounds()`
    (minus its own header height, for the cron editor and the Signal
    submenu) and calls `bottom_bar_attach()` in `_window_load`, destroying
    the returned layer in `_window_unload` — same shape as the main
    window's own `s_bottom_bar_layer`, just one static `Layer*` per window
    (`s_time_bottom_bar`/`s_cron_bottom_bar`/`s_edit_bottom_bar`/
    `s_signal_bottom_bar`). `handle_minute_tick` checks all five against
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
  - Itemized menus (`confirm_window`, `repeat_window`, `edit_window`, and
    every other `*_window` built the same way — `repeat_preset`, `cron`,
    `signal`, `pattern_select`, `custom_tune`) use
    `menu_layer_set_normal_colors`/`menu_layer_set_highlight_colors`
    (white/black, black/white) instead of manually painting each row —
    exactly timer's simpler "detail window" pattern (`dl_draw_row`), not the
    main list's per-row tinting. `edit_window` has no header — the label is
    its own "Label: <name>" row (opens the multitap keyboard) alongside
    State/Time/Repeat/Snooze/Vibration/Sound/Auto-dismiss/Delete.
    `pattern_select` is the one exception to the black/white highlight
    convention (its highlight colors deliberately match its normal colors —
    a no-op overlay — since `pattern_select_draw_row` paints each row's
    selected state itself, at whatever height that row actually needs
    rather than MenuLayer's own fixed-height automatic highlight fill).
    **Every one of these windows shares two small helpers** instead of each
    repeating its own copy: `itemized_menu_create()` folds in
    `menu_layer_create`/`set_callbacks`/`set_normal_colors`/
    `set_highlight_colors`/`layer_add_child` (callers still own click-config
    wiring — some use the plain `menu_layer_set_click_config_onto_window`,
    several instead use their own `window_set_click_config_provider` for
    continuous-scroll or long-press — and whatever header/bottom-bar chrome
    surrounds the menu, since those genuinely differ per window); and
    `menu_header_create()` replaces what used to be a separate
    `*_header_update_proc` per window (a plain white strip with a centered
    bold title) — these were **byte-for-byte identical except the title
    string**, confirmed directly in the compiled binary: the linker's own
    identical-code-folding had already collapsed `repeat_header_update_proc`
    into a 4-byte stub of `repeat_preset_header_update_proc`, since both
    happened to draw the literal string `"Repeat"`; the other titles differ,
    so the linker couldn't fold those, but a shared function could.
    `menu_header_create`'s title is carried as **per-layer data**
    (`layer_get_data`), not a shared static — several of these windows can be
    simultaneously stacked (e.g. edit → signal → pattern_select →
    custom_tune, all loaded at once, just obscured), and BACK popping the
    topmost one forces the newly-exposed window's layers to redraw; a single
    shared "current title" variable would already have been overwritten by
    whichever window was on top, mis-rendering the one beneath once exposed
    again. Consolidating this (plus reusing the existing `draw_key_value_row`
    helper, defined alongside `signal_draw_row` below, in `edit_draw_row`/
    `repeat_preset_draw_row`/`repeat_draw_row`, which used to each duplicate
    its font/`GRect`/draw-call boilerplate inline instead) recovered just
    over 1KB in the `APP_TEST_HOOKS=1` build.
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
  staged fields against real "now" — its time portion is run through
  `bottom_bar_compact_ampm()` (same as the bottom bar's own "Next: ..."
  text), so a 12h-style time reads as e.g. "6:00p" rather than "6:00 pm",
  since this row is just as tight on horizontal space, sharing its line
  with the "Apply" label itself — blank if any field is currently
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
  Snooze, Signal, Auto-dismiss — Delete leads (so it's the row selected by
  default when the menu opens; still gated behind its own confirm prompt,
  so landing on it isn't destructive by itself), and Time/Cron comes next
  ahead of Label/State, unlike every other row grouping in this app which
  puts Label first.
- **Signal submenu (`EDIT_ROW_SIGNAL`) groups Vibration/Sound/Increasing
  volume/Vibe pattern/Sound pattern under one edit-menu row** instead of
  five separate top-level rows — `signal_edit_window_push()` opens a second
  itemized `MenuLayer` (same bottom-bar-bearing shell as `edit_window`
  itself, plus its own "Signal" header, `signal_header_update_proc`)
  operating on the same `s_edit_idx` alarm. The submenu itself has only
  **3** rows now — `SIGNAL_ROW_VIBE_PATTERN` ("Vibration"),
  `SIGNAL_ROW_SOUND_PATTERN` ("Tune"), `SIGNAL_ROW_INCREASING_VOLUME` (a
  4th, `SIGNAL_ROW_PATTERN_HELP`, used to live here too — a single combined
  "Pattern/Tune help" row; removed in favor of each picker having its own
  trailing "Help" row instead, see `pattern_select_window_push()` below).
  The standalone Vibration and Sound on/off toggles were **merged into**
  the Vibration/Tune rows respectively
  (removed as separate rows entirely, each row taking over its toggle's old
  slot: Vibration sits where the old Vibration toggle used to, Tune sits
  where Sound used to) rather than living alongside them;
  `signal_draw_row`'s Vibration/Tune value reads "Off" outright whenever
  `vibration_enabled`/`sound_enabled` is false, without even looking at the
  underlying pattern/tune fields. `Increasing volume` still toggles in
  place on SELECT, moved verbatim from the old top-level row. (The row's
  internal constant/enum names still say `VIBE_PATTERN`/`PATTERN_SELECT_
  VIBE` throughout `main.c` — only the user-facing label changed to
  "Vibration", the same way `SIGNAL_ROW_SOUND_PATTERN` has always displayed
  as "Tune".)
  - **`pattern_select_window_push()`** is the one shared picker window
    (differing only in row count/labels) both Vibration and Tune open.
    Row 0 of either is **"Off"** — the merged on/off toggle, the ONLY
    instant-apply row in either picker: selecting it just flips
    `vibration_enabled`/`sound_enabled` to false and pops straight back to
    the Signal menu, deliberately leaving every other field
    (`vibe_pattern_kind`/`vibe_pattern`/`vibe_pattern_custom`/
    `sound_pattern_kind`/`sound_preset`/`custom_tune_id`) untouched —
    so turning it back on later still remembers whichever pattern/tune was
    set before. `PATTERN_SELECT_VIBE_ROW_COUNT`/`PATTERN_SELECT_SOUND_ROW_COUNT`
    and the `_CUSTOM_ROW` constants all account for this extra leading row
    (every preset/Custom row index is its old index + 1).
  - **Both Vibe's and Sound's preset rows commit immediately** from
    `pattern_select_select` itself, straight back to Signal with no
    keyboard step at all — the audible/haptic preview already happened as
    the row was navigated to (`pattern_select_preview_current_row`), so
    there's nothing left to review by the time Select is pressed. This is
    a later unification: Vibe's presets used to open the pattern-mode
    keyboard pre-filled with that row's text instead (selecting a preset
    was "start from this template", not "apply immediately"), but that was
    changed on request to match Sound's own presets, which had always
    committed instantly (see `pattern_select_select`'s own comment: "on
    request, matching Tune's own presets below"). A selected Vibe preset
    (rows 1..`VIBE_PRESET_COUNT` into the `VIBE_PRESETS[]` table, below)
    still commits as `vibe_pattern_kind=1` with that preset's exact bytes
    rather than a separate stored index, which `vibe_preset_match()`
    re-matches by byte-identity to display it by name. The Vibration picker is
    laid out like the Tune picker: Off, presets, one row per library
    vibration, "+ Custom" (creation wizard), Help — see "Custom-vibration
    library" below (the old inline "Custom" row that opened the keyboard
    pre-filled is gone). Selecting a Sound
    preset row (rows 1..`SOUND_PRESET_COUNT` into the `SOUND_PRESETS[]`
    table, below) sets `sound_pattern_kind=0`,
    `sound_preset=<that row's index>`, `sound_enabled=true`. `sound_preset`
    is a plain index into `SOUND_PRESETS[]`
    (`alarm_calc.h`, mirroring `vibe_pattern`'s own approach) — sound used
    to instead copy the preset's parsed bytes verbatim into its own
    `sound_pattern_kind=1` ("Custom") representation, but tracing every
    call site that could reach that path found it could only ever be one
    of these exact table entries, never genuinely freehand (see the
    Custom-tune-adjacent design point below for the removal). A sound tune
    row and "+ Custom tune" are unaffected by any of this — they still go
    through their own intermediate step (the Custom-tune screen, the
    2-step creation wizard respectively) same as before.
    Committing a pattern also sets `vibration_enabled`/`sound_enabled` back
    to `true` — picking a real pattern implies turning that channel back
    on, symmetric with "Off" leaving it alone rather than the other way
    around. `pattern_select_window_push()`'s preselect logic checks
    `vibration_enabled`/
    `sound_enabled` FIRST (landing on row 0 if either is false) before
    falling through to the existing preset-match logic (an `a->sound_preset`
    index lookup for sound, `vibe_preset_match()`'s byte re-match for vibe).
  - **Each picker's trailing row is its own "Help"** —
    `PATTERN_SELECT_VIBE_HELP_ROW` (`VIBE_PRESET_COUNT + 2`, right after
    Custom) for Vibration, `pattern_select_sound_help_row()` (right after
    "+ Custom tune", dynamic like every other SOUND row index since it
    depends on `s_tune_count`) for Tune — replacing the single combined
    "Pattern/Tune help" row that used to live in the Signal submenu itself
    (see that design point above). Selecting it pushes `pattern_help_
    window_push(kind)` ON TOP of the picker (same push-not-pop convention
    a tune row/"+ Custom tune" already use), so BACK returns to the picker
    rather than all the way to Signal. `pattern_help_window` is one shared
    window instance for both (`s_pattern_help_kind`, set right before push,
    picks the header title — "Vibration help"/"Tune help" — and which of
    `PATTERN_HELP_TEXT_VIBE`/`PATTERN_HELP_TEXT_SOUND` to show), split into
    two texts rather than one combined one now that each has its own
    screen: vibe's grammar has no pitch, so its text drops sound-only
    content (the note-letter requirement, octave marks) entirely instead
    of describing rules that don't apply to what the user is currently
    looking at.
  - Both pickers' Help row is excluded from `pattern_select_preview_
    current_row`/expand-on-navigate/pattern-lookup — `pattern_select_sound_
    row_pattern()` already returns `false` for any row past "+ Custom
    tune" (Help sits one further index past it) with no code change
    needed, but `pattern_select_vibe_row_text()` needed an explicit guard
    (`row == PATTERN_SELECT_VIBE_HELP_ROW` → empty string) — without it,
    the row's index would fall through to `VIBE_PRESETS[row - 1]`, reading
    one past the end of that array.
  - **Navigating to any row (either picker) plays that pattern once as a
    preview** — `pattern_select_preview_current_row()` reads whichever row
    is currently highlighted and, if it's not "Off", builds its text
    (`pattern_select_vibe_row_text()` for vibe, `pattern_select_sound_row_
    pattern()` for sound — a preset's fixed text, or (sound only, since
    tunes are inlined into the picker — see the Custom-tune library design
    point below) a tune row's own formatted pattern) and plays it via
    `alarm_preview_vibe_pattern()`/
    `alarm_preview_sound_pattern()` (parses on the fly, calls the same
    `alarm_play_vibe_pattern()`/`alarm_play_sound_pattern()` ring-time
    uses). Called from `pattern_select_up_click`/`pattern_select_down_click`
    (after moving the selection) and once right after the picker first
    opens on its preselected row, so browsing either picker previews every
    tune/vibe as you land on it — not on Select, which just proceeds
    straight to the keyboard without playing anything itself (an earlier
    version previewed sound-only, and only on Select; both were moved/
    extended on request, since scrolling through a list to audition
    entries is the more natural place for this, and there's no reason
    vibration shouldn't get the same treatment sound does).
  - **`SOUND_PRESETS[]`/`VIBE_PRESETS[]`** (`main.c`, both declared well
    above the Signal-menu UI section so `alarm_play_audio()`/`alarm_
    vibrate()` can see them) are the Tune/Vibration preset libraries —
    `{ label, pattern }` pairs. Sound, currently: Triple (default)/Double/
    Triple fast/Analog bell/Celebration/Lullaby/March (this last one leans
    on the multi-octave-marker support above — its low A/F/C notes are
    each written 2-3 octaves down, e.g. `"4A,,,"`, to land in a register
    that actually sounds like a real bassline instead of the base octave).
    Vibe, currently: Double/Short/Long.
    Sound resolves which preset is active via a plain stored index
    (`Alarm.sound_preset`, an index into this very table — see the Signal
    submenu design point above for why this is an index rather than a
    byte copy) — no re-matching needed, unlike vibe: `vibe_preset_match()`
    re-parses each `VIBE_PRESETS[]` entry and `memcmp`s it against an
    alarm's stored *custom* pattern bytes to find which preset (if any)
    it's byte-identical to, since vibe's own Custom row can still hold
    genuinely freehand content that only *coincidentally* matches a preset
    — used both to preselect the right picker row on reopen and to show
    the preset's NAME (not raw pattern) in the Signal menu's Vibration
    value and picker Custom-row fallback. `pattern_preset_preview()`
    renders at most the first 2 space-separated tokens of a preset's
    pattern (plus "...") for the Vibration picker's own per-row value
    column (sound's collapsed rows show no preview at all any more — see
    the Custom-tune library design point below).
  - `alarm_vibrate()`'s `vibe_pattern_kind==0` fallback (a brand new,
    never-touched alarm, or one that committed a preset via the old
    instant-apply path before this rework) parses `VIBE_PRESETS[a->
    vibe_pattern]` fresh each time and plays it via `alarm_play_vibe_
    pattern()`, mirroring `alarm_play_audio()`'s Default-fallback
    unification (see the "Alarm sound" design point above) — so "what
    Double/Short/Long feels like" can never drift from what the Vibration
    picker itself offers/previews.
  - Neither the Signal submenu, the pattern-select picker, nor the pattern
    keyboard needs the generic on_confirm/on_cancel-plus-snapshot-revert
    convention every other editor in this file uses (time/repeat/cron/
    snooze) — all operate directly on `s_alarms[s_edit_idx]` since there's
    exactly one call site for each, and nothing is mutated by BACK/cancel
    alone (except the instant-apply "Off" row, which needs no revert since
    it doesn't touch anything BACK would need to restore).
- **Custom-tune library**: named, reusable sound patterns any alarm can
  reference by id instead of storing its own copy — a *live* reference, not
  a copy: renaming/editing/deleting a tune affects every alarm currently
  pointing at it. Persisted entirely separately from `s_alarms`/
  `STORE_SCHEMA` — `CustomTune` (`alarm_calc.h`: `id`/`name`/`pattern_len`/
  `pattern`/`pattern_note`, the same shape `Alarm`'s own inline custom sound
  already has) lives in `s_tunes[MAX_CUSTOM_TUNES]`/`s_tune_count`
  (`main.c`), loaded/saved via `store_load_tunes()`/`store_save_tunes()`
  (`alarm_store.c`) under their own `TUNE_STORE_SCHEMA`/`PERSIST_KEY_TUNE_*`
  keys (`alarm_store.h`) — a schema bump to `Alarm` doesn't force wiping the
  tune library, and vice versa. `Alarm.sound_pattern_kind` gained a third
  value, `2` ("Library"), alongside the original `0` ("Preset", now the
  only other value it ever takes — see the "Custom sound/vibe patterns"
  design point below for `1`'s later retirement): `Alarm.custom_tune_id`
  (appended at the very end of the struct, `STORE_SCHEMA` 16) holds which
  tune, resolved via `find_tune_by_id()` both at ring time
  (`alarm_play_audio()`) and in the UI (`signal_draw_row`'s Tune value).
  - **Navigation**: tunes are inlined directly into the Tune picker itself
    now, below the presets — there is no separate "Custom tunes" list
    window any more (it existed briefly during this feature's development
    and was removed on request as one screen too deep). The SOUND picker's
    row layout is `Off, presets..., <one row per saved tune>, "+ Custom
    tune"` — row count is dynamic
    (`pattern_select_sound_row_count()` = `PATTERN_SELECT_SOUND_TUNES_BASE`
    (`SOUND_PRESET_COUNT + 1`) `+ s_tune_count + 1`), replacing the old
    fixed `PATTERN_SELECT_SOUND_ROW_COUNT`/`_CUSTOM_ROW` constants (vibe's
    own `PATTERN_SELECT_VIBE_ROW_COUNT`/`_CUSTOM_ROW` are untouched — this
    whole redesign is sound-only). Selecting a tune row calls
    `custom_tune_window_push(s_tunes[i].id)` directly, pushed on top of the
    picker (see the singular screen below) — no intermediate list to pass
    through. Selecting **"+ Custom tune"** (the trailing row, at
    `pattern_select_sound_new_row()`) runs the same 2-step creation wizard
    described below.
  - **Collapsed rows show no pattern preview at all** — neither presets nor
    tunes — a deliberate change from the picker's earlier behavior (which
    showed every preset's first 2 tokens as a value column): the only
    thing a collapsed sound row shows besides its label is, for a tune row
    specifically, a literal **`"(custom)"`** right-aligned
    (`pattern_select_draw_row`) — a plain marker distinguishing a user tune
    from a built-in preset, not an abbreviation of anything else on the
    row (lowercase and parenthesized deliberately, to read as a quiet tag
    rather than a second label competing with the tune's own name). `Off`/
    "+ Custom tune" show nothing in that column either.
  - **Expand-on-navigate**: whichever preset or tune row is the *currently
    selected* row (checked via `pattern_select_row_is_selected()`, wrapping
    `menu_layer_get_selected_index`) expands to two lines instead of one —
    label on line 1, that row's full pattern text word-wrapped in
    `GOTHIC_14_BOLD` on line 2 (`draw_label_and_notes_row()`). This is where
    a pattern's actual notes become visible at all now, since the collapsed
    view no longer shows them. `Off`/"+ Custom tune" never expand (no
    pattern to show). `pattern_select_cell_height` returns a taller height
    for exactly this case, computed via `graphics_text_layout_get_content_
    size` on the wrapped notes text at `GOTHIC_14_BOLD` plus the label
    line's own height; every other row (including every non-selected
    preset/tune row) keeps the fixed 34px. This expand/collapse behavior is
    SOUND-only — vibe's picker keeps its old always-visible 2-token preview
    on every preset/Custom row, unchanged.
  - `pattern_select_sound_row_pattern(row, out, out_size)` is the one helper
    behind all of this — returns `false` for "Off"/"+ Custom tune" (nothing
    to preview/expand/play), else fills `out` with a preset's fixed text or
    a tune's `ac_sound_pattern_format()`-reconstructed text and returns
    `true`. `pattern_select_preview_current_row`'s SOUND branch now just
    calls this and previews unconditionally when it returns `true` — the
    old `sound_pattern_kind == 2`-only gate (needed back when tunes lived
    one level deeper with no per-tune row here) is gone; every tune row
    previews on navigate, same as a preset row always did.
    `pattern_select_window_push`'s preselect logic: `sound_pattern_kind ==
    2` now resolves `find_tune_by_id(a->custom_tune_id)` and preselects
    `PATTERN_SELECT_SOUND_TUNES_BASE + <that index>` directly (falling back
    to the "+ Custom tune" row if the id is somehow stale/not found), rather
    than landing on a shared "Custom" row the way it used to.
  - **"+ Custom tune"** runs the same 2-step wizard as before
    (`new_tune_pattern_done`/`new_tune_name_done`, staged in a transient
    `s_new_tune_draft` — relocated next to `s_tunes`/`s_tune_count` since a
    forward reference from `pattern_select_select`, which now triggers it
    directly, needs the variable defined before that point in the file):
    the pattern keyboard first, then the name keyboard. Cancelling (`text ==
    NULL`) at *either* step aborts the whole thing outright — same "any
    BACK aborts the flow" convention the "+ New alarm" wizard already
    uses — nothing is created. An unparseable pattern or an empty name
    (a saved tune always has one) re-shows the *same* keyboard with
    whatever was typed still in it, rather than a separate error screen,
    since the user is looking straight at what they typed. Finishing both
    steps allocates an id (`store_alloc_tune_id()`, mirroring
    `store_load_next_local_id`/`store_save_next_local_id`'s shape for
    `Alarm.id`), appends to `s_tunes`, persists, and pushes straight to the
    same singular screen below — selecting an existing tune and finishing
    this wizard both land in exactly the same place.
  - **Selecting an existing tune** (or finishing the wizard) opens the
    **"Custom tune"** singular screen: `Apply` / `Tune <value>` / `Name
    <value>` / `Delete`, operating directly on `s_tunes[s_custom_tune_idx]`
    (resolved by id at push time via `find_tune_by_id`) — there's no
    separate staging struct here, since by the time this screen exists the
    tune is always already a saved record. `Tune`/`Name` write straight
    through to the shared record immediately (`persist_tunes()` +
    `menu_layer_reload_data`, no "confirm" step) — the live-reference model
    means every other alarm referencing this id sees the change too, and
    BACK needs no snapshot-revert since nothing here is ever staged.
    `Apply` is the one action that actually links the *current* alarm
    (`s_edit_idx`, unchanged throughout this whole Signal → Tune picker →
    Custom tune chain) to the tune —
    `sound_pattern_kind=2`/`custom_tune_id=<id>`/`sound_enabled=true`, then
    pops **2** windows (this screen, the Tune picker itself) to land back
    on Signal — one fewer than before this redesign, since there's no
    intermediate list to pop through any more. `Delete` (`confirm_window_
    push`, same shell `edit_window`'s own Delete uses) removes the tune
    from `s_tunes` entirely and calls `revert_alarms_using_tune()` — every
    alarm with `sound_pattern_kind==2` pointing at this id reverts to
    `sound_pattern_kind=0` (Preset — whichever `sound_preset` index it last
    had, or 0/"Triple (default)" if never set; `sound_enabled` is left
    untouched) —
    then pops only **1** window, back to the **Tune picker** itself (not
    all the way to Signal like Apply: deleting is "keep managing the
    library", applying is "I'm done"); since the tune's own row just
    disappeared from the picker, its selection is also reset to row 0
    ("Off") rather than risk pointing past the now-shorter row count.
- **Persist key map** (watch storage; `alarm_store.h`, plus the keyboard's own
  block): scalars 1-13, 14-16 (tune schema/count/next id), 17-19 (default
  sound), 22 (armed wakeup), 23-25 (vibration schema/count/next id); alarms
  100-115, alarm extensions 200-215, tunes 300-319, **the vendored keyboard's
  settings block 400-411** (`MULTITAP_SETTINGS_BASE_KEY`, in this app's own
  namespace — key wait time default 800 ms, auto-caps, extended characters,
  theme; global to every keyboard in the app, edited via UP in any keyboard),
  vibrations 500-519. An earlier revision put the vibration library at 400+i:
  with a library entry at index 0 the keyboard read its blob as the "key wait
  time" (150 ms), and saving keyboard settings overwrote library entries. Fixed by
  moving the library to 500+; `store_load_vibes()` moves any entry still at the
  old keys (recognised by their exact blob size) and `migrate_inline_vibes()`
  recreates one the keyboard already overwrote from the alarm's retained inline
  copy. Because the keyboard overwrote library slots with plain numbers (library
  positions 1, 2, 4 and 11 -- its keys 400, 401, 403, 410), the move left blank
  entries behind; `store_load_vibes()` therefore keeps only entries
  `ac_custom_vibe_is_valid()` accepts (non-zero id, named, 1-8 sane segments, no
  duplicate id), compacts them and rewrites the table at once. An entry the
  keyboard overwrote is unrecoverable by itself; the alarm that used it gets a new
  one ("<alarm> vibe") from its retained inline copy, and an entry no alarm used
  is gone. Verified by reproducing the old layout on the emulator, changing the
  keyboard's wait time/auto-caps, then installing the fixed build over it.
  `tests/keys.test.js` and `_Static_assert`s in `alarm_store.c` keep every
  range disjoint — add any new persist key to `alarm_store.h` and run it.
- **Custom-vibration library** (vibration counterpart of the tune library;
  `CustomVibe`/`MAX_CUSTOM_VIBES` 20, `alarm_calc.h`): named, reusable vibration
  patterns an alarm references by id — a live reference, not a copy.
  `Alarm.vibe_pattern_kind` now has three values: 0 = preset (`vibe_pattern`
  index), 1 = inline bytes in `vibe_pattern_custom[]` (what a picked
  Double/Short/Long preset is stored as, re-matched by `vibe_preset_match()`),
  2 = **library** (`Alarm.custom_vibe_id`, appended at the end of the struct,
  `STORE_SCHEMA` 21 — a pure append, zero-fills safely). The library lives in
  `s_vibes[]`/`s_vibe_count` (`main.c`) with its own persisted table
  (`store_load_vibes`/`store_save_vibes`/`store_alloc_vibe_id`, keys 23-25 and
  500+i, `VIBE_STORE_SCHEMA`), like the tunes. `alarm_vibrate()`/
  `alarm_vibe_duration_ms()` resolve kind 2 (a stale id falls through to the
  preset). UI mirrors the Tune picker: library rows inlined in the Vibration
  picker (`pattern_select_vibe_*` helpers), selecting one opens the "Custom
  vibration" screen (Apply/Pattern/Name/Delete, `custom_vibe_*`), "+ Custom"
  runs the pattern-keyboard -> name-keyboard wizard (`new_vibe_*`), Delete
  reverts alarms using it to a preset (`revert_alarms_using_vibe`). Hand-typed
  inline vibrations from before the library are moved into it at launch
  (`migrate_inline_vibes()`: identical bytes share one entry, named
  "<alarm name> vibe"; a preset-identical inline pattern is left alone; if the
  library is full the alarm keeps its inline copy, which still plays). Synced
  both ways like tunes: key `VibeConfig` (snapshot stage 1, before tunes and
  alarms), and an alarm record carries two extra optional fields — field 15
  `vibeId`, field 16 `vibePreset` (0-2) — so the page's per-alarm Vibration
  select (Double/Short/Long + library vibrations) round-trips; a push only
  rewrites the watch's stored vibration when it actually differs.
  **Rule: never bump `VIBE_STORE_SCHEMA` (or `TUNE_STORE_SCHEMA`) without a
  migration.** `store_load_vibes()`/`store_load_tunes()` wipe the whole library on
  ANY schema mismatch, and an alarm whose `custom_vibe_id`/`custom_tune_id` no
  longer resolves silently falls back to a preset — the pattern is lost (the
  alarm's schedule is unaffected). A library layout change must instead keep old
  data readable, the way `store_load()` does for alarms (read the old size into a
  zeroed current-sized buffer; only a genuine hard break may reset, and then say
  so here). Deliberately NOT done: playing a migrated alarm's retained inline copy
  (`vibe_pattern_custom[]`, left in place by `migrate_inline_vibes()`) when the
  library entry is missing — only migrated alarms have one, and it goes stale as
  soon as the library entry is edited, so it would be a partial, misleading
  safety net. Verified the schema 20 -> 21 upgrade over real persisted data
  (alarms, a tune, inline vibrations; installed the new build over an old
  test-hook build without wiping): all alarms/tune survived, identical inline
  patterns became one shared entry, a preset-identical one was left alone.
- **Custom sound/vibe patterns are a genuine ABC-notation subset** (reworked
  by the "ABC notation" commit, `STORE_SCHEMA` 20 -- see below; this replaced
  an earlier, ad-hoc "digit = fixed 100ms unit, `-` = break" grammar with
  real ABC tokens throughout). A pattern is a space-separated sequence of
  on-beats and (optional) rests, each carrying a duration expressed as a
  **ratio** rather than a fixed digit. Sound (Tune) and vibe (Vibration)
  still have DIFFERENT segment caps -- `AC_SOUND_PATTERN_MAX_SEGMENTS` (32)
  and `AC_VIBE_PATTERN_MAX_SEGMENTS` (8), `alarm_calc.h` -- unchanged by this
  rework.
  - **Rest**: real ABC's own rest token, `z`, optionally followed by a
    duration suffix (e.g. `"z"`, `"z/2"`) -- replacing the old digit+`-`
    break spelling entirely; there is no other way to spell a rest now.
  - **Duration**: an optional `[N]['/'D]` ratio suffix, where `N` and `D`
    are each one of `AC_NOTE_RATIO_DIGITS` (`{1..8}`, `alarm_calc.c`/`.h`) --
    "0"/"9" are plain ASCII digits but invalid ratio digits, same exclusion
    the old grammar had for its duration digit. `N`/`D` are reduced to
    lowest terms by their GCD before packing, so two spellings of the same
    ratio (`"1"` and `"2/2"`) always produce the identical stored byte --
    this matters because `vibe_preset_match()` (`main.c`) identifies a
    preset by `memcmp`-ing raw bytes. Actual duration is
    `round(AC_NOTE_DEFAULT_MS * N / D)` ms, via `ac_pattern_duration_ms()`
    (`alarm_calc.c`) -- `AC_NOTE_DEFAULT_MS` is 100, so a bare on-beat/rest
    still means exactly 100ms, matching the old grammar's own per-digit
    unit; only sound's on-beat can spell a truly bare (letter-only) token,
    since its note letter anchors the suffix -- vibe's on-beat token IS the
    duration suffix in its entirety, so it can never be zero-length and
    must always spell an explicit ratio (e.g. `"1"` for the same 100ms).
    Packed as `(N-1)*8 + (D-1)` into bits 0-6 of the stored byte
    (`AC_PATTERN_DURATION_MASK`); bit 7 (`AC_PATTERN_REST_BIT`) marks a
    rest.
  - **On-beat for VIBE** is just the (possibly bare) duration ratio itself,
    e.g. `"2"`, `"1/2"`, `"3/4"` -- vibe has no pitch, so nothing precedes
    the ratio.
  - **On-beat for SOUND** is `[accidental]letter[octave-marker(s)][ratio]`:
    an optional leading accidental (`^` sharp, `_` flat -- real ABC
    notation, `AC_NOTE_ACCIDENTAL_SHARP`/`_FLAT`; no prefix means natural,
    `AC_NOTE_ACCIDENTAL_NATURAL`), one of the 7 note letters `C D E F G A B`
    (`AC_NOTE_LETTER`, MIDI 96-107 base octave via `AC_NOTE_MIDI`), then
    optional octave marker(s) right after the letter -- `'` (apostrophe) up,
    `,` (comma) down, real ABC-notation octave shift convention, e.g. `"C"`
    = base, `"C'"` = up 1, `"C,,,"` = down 3 -- then the optional duration
    suffix. **Octave markers can stack**: `,` may repeat up to
    `AC_NOTE_OCTAVE_MAX_DOWN` (8) times, but `'` only once
    (`AC_NOTE_OCTAVE_MAX_UP` = 1) -- this asymmetry keeps every one of the 7
    notes' resulting MIDI value inside 0-127: C(96) is the binding
    constraint going down (`96 - 8*12 = 0` exactly; a 9th `,` would go
    negative), A(105)/B(107) the binding constraint going up (2 octaves up
    would be 129/131). A run must be all the same character -- `"C',"`
    (mixed) is rejected, same all-or-nothing philosophy as every other
    malformed-token case in this grammar; a run past its own cap is a parse
    error, never clamped. `alarm_build_sound_notes()` (`main.c`) adds a
    defensive `[0,127]` clamp on the final computed MIDI value anyway,
    belt-and-suspenders. A bare duration-only token (no accidental/letter)
    is valid for vibe but always rejected for sound -- sound must always
    name a pitch (or be a rest) -- the one structural difference between
    the two grammars. Deliberately, every flat pitch is enharmonically
    reachable as some other note's sharp, but both accidentals are
    supported anyway -- a readability choice (spelling a pattern the way
    it's actually thought of), not a reachability gap.
  - Two on-tokens with nothing between them play back to back with no gap
    (e.g. sound `"^C/2 z C3 _D"`, vibe `"1 z/2 1"`).
  - Parsed by `ac_vibe_pattern_parse()`/`ac_sound_pattern_parse()`
    (`alarm_calc.c`, host-testable, no SDK dependency, sharing an internal
    tokenizer/duration-ratio parser) into a packed byte per segment; sound
    additionally fills a parallel `sound_pattern_note[]` value, encoded
    *arithmetically* (not bit-packed) as `(octave*AC_NOTE_COUNT +
    note_index)*AC_NOTE_ACCIDENTAL_COUNT + accidental` (max 69*3+2=209,
    comfortably inside a `uint8_t`) -- meaningless on a rest segment.
    `AC_NOTE_OCTAVE_SAME` (8, chosen as `AC_NOTE_OCTAVE_MAX_DOWN`) is the
    octave field's zero point; applied as `AC_NOTE_MIDI[base] + 12 *
    (octave - AC_NOTE_OCTAVE_SAME)`.
  - Any malformed token (bad ratio digit, missing/invalid note letter, a
    bare duration-only token for sound, an octave-marker run past its cap
    or mixing `'`/`,`, more than the applicable cap's segments, trailing
    garbage after the ratio) fails all-or-nothing, same convention as
    `ac_cron_parse_field`. Unlike cron's raw-text-plus-mask duplication, no
    raw string is persisted at all -- `ac_vibe_pattern_format()`/
    `ac_sound_pattern_format()` deterministically reconstruct the canonical
    text (used for both display and re-editing) from the stored bytes, an
    exact round trip since there's no lossy truncation step in this
    grammar; a reduced ratio always formats back to its own lowest-terms
    spelling (e.g. a byte originally parsed from `"2/2"` reformats as
    bare), so re-opening a saved pattern for editing can show different
    text than was typed, never different audio/vibration.
  - `Alarm.vibe_pattern_kind` (0 = Preset, use `vibe_pattern`; 1 = Custom,
    use `vibe_pattern_len`/`vibe_pattern_custom[]`) still gates which
    representation is live for vibe. **Sound has no inline-Custom
    representation** -- `Alarm.sound_pattern_kind` only ever takes `0`
    (Preset, use `sound_preset`, an index) or `2` (Library, use
    `custom_tune_id`); this whole packed-byte grammar survives on the sound
    side only through `CustomTune.pattern[]`/`pattern_note[]` (the tune
    library) and `Alarm.vibe_pattern_custom[]`, not through anything on
    `Alarm` itself for sound (see `STORE_SCHEMA` 19's own removal of
    `Alarm.sound_pattern[]`/`_len`/`_note[]`, still true under this
    rework).
  - **`STORE_SCHEMA` 20** is this ABC-notation rework itself -- a hard break
    on the byte *encoding* only (old digit-duration/`-`-break bytes mean
    something different under the new ratio/rest scheme, and sound's
    `sound_pattern_note[]` encoding changed shape too), so `store_load()`
    resets the affected `*_kind`/`*_len` fields to their safe defaults on
    migration exactly like every prior signal-encoding bump (12-19) did --
    it does **not** wipe the alarm's schedule (time/repeat/cron), per the
    EXTENSION-blob-vs-MAIN-blob split described above (`ext_schema_is_
    appendable()`). An unparseable pattern typed in
    the library wizards (`new_vibe_pattern_done`/`custom_vibe_pattern_done`, and
    the tune equivalents) never touches persisted state: the same keyboard is
    re-shown with the text still in it. Applied at ring
  time by `alarm_vibrate()`/`alarm_play_audio()` (`main.c`): sound builds
  one `SpeakerNote` per segment directly (each carries its own explicit
  duration/pitch, so consecutive on-beats need no special handling — they
  just play as distinct back-to-back notes); vibe first run-length-encodes
  consecutive same-kind segments into merged on/off durations (since the
  SDK's `VibePattern` array must strictly alternate on/off starting with
  ON, unlike this grammar's optional breaks) before calling
  `vibes_enqueue_custom_pattern()`.
  - **Both sound and vibe playback always prepend ~100ms of silence before
    the real pattern**, a deliberate workaround for an observed glitch
    where the very first note/pulse of a sequence sometimes didn't play —
    added at the actual-hardware-playback layer only, never at
    parse/format/storage, so neither the persisted bytes nor the text shown
    in the picker/keyboard/Signal menu ever reflects this extra break.
    `alarm_play_sound_pattern()` (`main.c`) is the single shared function
    behind this for sound — it prepends one silent (`SpeakerWaveformSine`,
    midi 0, 100ms) `SpeakerNote` ahead of `alarm_build_sound_notes()`'s
    real segments — called from both `alarm_play_audio()` (ring time, for
    either Default or a stored Custom pattern — the two converged onto this
    one function, so "what Default sounds like" and "what a byte-identical
    Custom pattern sounds like" can never drift apart even with this
    workaround layered on top) and `alarm_preview_sound_pattern()`
    (Tune-picker preview-on-select, above). `alarm_play_vibe_pattern()`
    is vibe's equivalent, called from `alarm_vibrate()`'s custom-pattern
    branch only (not the plain preset one-shots
    `vibes_short_pulse()`/`_long_pulse()`/`_double_pulse()`, which are
    single pulses with no "first note of a sequence" concern to begin
    with) — it always pads a 0ms dummy ON (VibePattern's array must start
    ON) followed by a 100ms OFF gap, MERGED into the real pattern's own
    leading break (summed durations) if it has one, rather than inserted as
    a redundant separate OFF entry that would violate the array's required
    strict on/off alternation.
  - **Repeated buzz cycles size their own gap to the actual pattern
    duration, not a bare fixed interval** — a real bug fix, not a
    theoretical one: `alarm_buzz_cb()` (`main.c`) used to always schedule
    the next cycle at a flat `ALARM_BUZZ_INTERVAL_MS` (4s) regardless of
    how long the currently-playing vibe/sound pattern actually takes. A
    custom Tune/Vibration pattern can easily run longer than 4s (e.g. the
    "Imperial March" preset) — when it does, the fixed interval either let
    the next cycle's playback call queue up behind/overlap the still-
    running previous one (drifting vibration and sound out of sync with
    each other more and more every cycle, since the two channels don't
    necessarily take the same time), or — after a first fix pass added an
    unconditional `vibes_cancel()`/`speaker_stop()` at the top of every
    cycle specifically to stop that drift — cut a pattern's own tail off
    early the instant it ran even slightly past 4s, since the timer fired
    and force-stopped it before it naturally finished. `alarm_vibe_
    duration_ms()`/`alarm_sound_duration_ms()` (`main.c`) compute a
    pattern's real total playback time (mirroring `alarm_vibrate()`/
    `alarm_play_audio()`'s own default/custom/tune-library branching
    exactly via the shared `pattern_duration_ms()` helper, plus each
    channel's own fixed ~100ms lead-in from the workaround above, so "what
    plays" and "how long it takes" can never disagree) — `alarm_buzz_cb()`
    now sizes each cycle's own next-timer delay to
    `max(ALARM_BUZZ_INTERVAL_MS, longer-of-{vibe,sound} duration +
    ALARM_BUZZ_MIN_GAP_MS)`, so a short/default pattern keeps the familiar
    ~4s cadence unchanged while a long one gets exactly the runway it
    needs, plus a fixed minimum silent gap (`ALARM_BUZZ_MIN_GAP_MS`, 2s —
    long enough to read as a deliberate pause, not "the next one started
    immediately"), before the next cycle starts. The `vibes_cancel()`/
    `speaker_stop()` pair from the first fix pass stays in place, now just
    a defensive safety net against any remaining drift (e.g. a ramping-
    volume replay taking marginally longer than its own calc expected)
    rather than the primary mechanism.
    - **`increasing_volume`'s early cycles don't actually make sound at
      all** — `alarm_current_volume()` returns 0 for the first couple of
      buzz cycles (see its own doc comment above), and
      `alarm_play_sound_pattern()` itself no-ops outright when `volume ==
      0`. Counting the tune's full duration toward the gap on one of those
      silent cycles would pad the wait for a sound that was never going to
      play anyway, slowing down the ramp-up for no reason. `alarm_buzz_cb()`
      computes THIS cycle's volume once (`sound_volume`, also gating on
      `speaker_is_muted()` the same way `alarm_play_sound_pattern()` does)
      and only factors `alarm_sound_duration_ms()` into the gap when that
      volume is actually nonzero — reusing the same `sound_volume` for the
      real `alarm_play_audio()` call right below, so the gap calculation
      and the actual playback can never disagree about whether this cycle
      makes sound.
- **The pattern-entry keyboard is a `multitap_keyboard` mode**
  (`multitap_keyboard_set_pattern_mode()`), a sibling of cron's existing
  numeric mode rather than a restyle of it -- reworked alongside the ABC
  grammar above so its glyphs now match ratio/rest/accidental/octave
  tokens instead of the old digit+`-` scheme. Both cron's numeric mode and
  pattern mode lock the layout, but numeric mode's character keys are plain
  digits 1-9 and its bottom-left key cycles `*`/`-`/`/`/`,`; pattern mode's
  character keys instead pull from one of two small, purpose-built tables
  selected by a second flag, `pattern_notes_enabled`:
  - **`PATTERN_KEY_SOUND[9]`** is SOUND's primary ("note layout") page, a
    plain 3x3 grid, keys 0-8 row-major: the 7 note letters `C D E F G A B`
    one per key (0-6), key 7 (bottom-center, above Space) cycling the
    octave/accidental glyphs `'`/`,`/`^`/`_` (real ABC octave-up/down and
    sharp/flat, one tap-cycle since they now share a key rather than the
    bottom-left "Shift" key), and `z` (the rest token) last on key 8.
    Duration digits are deliberately NOT on this grid -- they're reachable
    two other ways: long-pressing any key inserts its keypad digit
    regardless of that key's tap-time glyph, and pressing DOWN cycles to a
    second, SOUND-only **numbers page** (`PATTERN_KEY_SOUND_NUMBERS[9]`,
    reusing `N0`..`N7` -- plain "1".."8" one per key 0-7, key 8 left empty
    since `AC_NOTE_RATIO_DIGITS` caps at 8). The bottom-left key cycles only
    `/` (`PATTERN_SYMS`, now a single-symbol cycle -- it used to also carry
    `-`/octave markers before key 7 was freed for them).
  - **`PATTERN_KEY_VIBE[9]`** is VIBE's one and only page, a deliberately
    different shape (not a smaller SOUND grid) -- vibe has no letters to
    make room for, so every key gets exactly one glyph, no cycling at all:
    digits 1-8 row-major on keys 0-7, then `z` last on key 8 (matching
    SOUND's own `z` position). A plain tap always types the intended
    character directly. The bottom-left key still cycles `/` only, same as
    SOUND.
  - Both SOUND and VIBE also cycle to a **navigation** sub-page via DOWN
    (SOUND: notes -> numbers -> nav; VIBE: digits -> nav), whose keys 3/5
    move the text cursor left/right instead of typing anything -- the same
    `PAGE_NAV`/`prv_locked_page_count`/`prv_on_nav` mechanism the plain/
    label keyboard and cron's numeric mode share (see below).
  Pushed via `multitap_keyboard_window_push_pattern(..., notes_enabled)`,
  capped at `PATTERN_KEYBOARD_MAX_LEN` (160, `main.c`, shared by both
  keyboards so sized for the larger of the two) -- worst case
  `AC_SOUND_PATTERN_MAX_SEGMENTS` (32) segments of up to several chars each
  (accidental + letter + up to 8 octave markers + a `N/D` ratio); vibe's
  own `AC_VIBE_PATTERN_MAX_SEGMENTS` (8) cap needs far less. The vendored
  widget's own `BUFFER_CAP` (`multitap_keyboard.c`) must exceed this cap.
  The text field itself renders through the same shared
  `prv_draw_text_fitted()` ladder every other mode uses, no different
  treatment for pattern mode -- an earlier version of this feature bolded
  on-beat tokens vs. rests via a separate `prv_draw_pattern_text()`
  renderer, patching the widget's own draw routine to work around Pebble's
  lack of a rich-text/span primitive, but that distinction turned out not
  to be needed and was reverted; if a future feature needs per-token
  styling again, this is the precedent for how invasive that gets (the
  most invasive change made to this vendored widget when it existed --
  every other extension only ever added a behavior flag, never touched
  drawing). An even earlier version put the octave markers on each note
  key's own cycle (baked-in `C`/`C^`/`Cv`-style variants, needing
  lowercase `c`/`d` duplicates on two spare keys to reach a second octave
  at all) rather than on their own key -- moving them (and later the
  accidentals too) to key 7 kept each note key's cycle to one glyph and
  freed key 8 for `z`, at the cost of one more keypress-and-commit step to
  shift a note's octave or add an accidental; that earlier approach also
  needed the widget's own shift-casing explicitly disabled in pattern mode
  (to stop auto-caps from silently uppercasing the lowercase duplicates) --
  with no lowercase glyphs left anywhere in pattern mode now, that guard
  was dead code and has been removed from `prv_render_glyph`/`prv_key_label`.
- **Main list display**: the bold time slot (`ml_format_cron_time`) shows
  the hour/minute formatted via `ac_format_time` — same
  `clock_is_24h_style()`-aware `"HH:MM"`/`"H:MM am"` every non-cron alarm's
  own time uses (e.g. hour `"9"` + minute `"5"` reads as `"09:05"` or
  `"9:05 am"`) — only when BOTH fields are plain decimal numbers; if either
  is a genuine pattern (e.g. `"*/20"`, `"9-17/2"`), there's no single
  configured time to show two numbers for, so the whole slot instead shows
  the day's earliest matching occurrence (`cron_first_hour_minute` —
  lowest set bit in `cron_hour_mask`/`cron_min_mask`, each considered
  independently since the two masks don't interact), same formatting, with
  a trailing `+`, e.g. `"9:05 am+"`, to mark it as just the first of
  potentially several fires that day rather than the alarm's one and only
  time; the summary line below still shows all six raw cron fields
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
  layout for its digits and repurposes the bottom-left key (normally Shift)
  to multi-tap-cycle `*`/`-`/`/`/`,` through the same `prv_eff_glyphs`/
  pending-key/commit machinery every other key already uses, rather than a
  parallel code path — so it inherits the usual multi-tap highlight/
  underline/commit-on-timeout behavior for free. Down no longer early-returns
  here — it cycles a small locked sub-page range shared with pattern mode
  (`locked_sub_page`, `prv_locked_page_count`/`prv_on_nav`, both in
  `multitap_keyboard.c`): a "navigation" sub-page whose keys 3/5 (middle-row
  left/right) move the text cursor left/right instead of typing anything,
  drawn as small arrow icons (`prv_draw_nav_arrow`, a stroked shaft+chevron,
  not a filled `GPath` asset). The same navigation
  sub-page exists for the plain/label keyboard too (a real 4th `Page` value,
  `PAGE_NAV`, appended after Symbols) and for pattern mode's own SOUND/VIBE
  sub-pages — see `multitap_keyboard.c`'s own doc comments (`cursor_pos`,
  `prv_append`/`prv_utf8_backspace`) for the full cursor-position model.
  Tapping the text field directly (`multitap_keyboard_handle_tap` ->
  `prv_text_field_hit`) also places the cursor, byte-precisely: exact
  boundary-snapping in the single-line display (`prv_snap_offset`), a small
  greedy word-wrap pass of its own to find the line break in the wrapped
  display, and a geometric (nearest-end) fallback only in the front-
  truncated display, which has no real position to snap to for its hidden
  leading text. Holding either navigation key repeats the move, the same
  hold-to-repeat treatment the on-screen DEL key already gets: the touch
  driver (`multitap_keyboard_window.c`) checks
  `multitap_keyboard_point_is_nav_key()` (mirroring
  `multitap_keyboard_point_is_delete()`) right alongside its existing DEL
  check, and if the held point is one of these two keys, calls
  `multitap_keyboard_move_cursor()` once immediately and then keeps calling
  it on the same `multitap_keyboard_delete_repeat_ms()` cadence until
  liftoff — reusing the exact `s_del_repeat_timer` pattern (its own
  `s_nav_repeat_timer` twin) rather than inventing a new mechanism.
  `multitap_keyboard_handle_hold()` itself also refuses keys 3/5 while
  `prv_on_nav()`, even though the touch driver never actually reaches it for
  those two keys anymore — belt-and-suspenders, since that function's
  generic long-press-inserts-a-keypad-digit behavior (unmodified for every
  other key) would otherwise type "4"/"6" there, which never had a glyph to
  begin with.
- **On-watch help texts are raw app resources**, not compiled-in string
  constants: the cron / pattern-vibe / pattern-sound help screens (`main.c`)
  and the keyboard's own "How to type" screen (`keyboard_settings.c`) load
  their text via `load_help_text()` from `resources/text/*.txt` (declared in
  `package.json`; those `.txt` files are the source of truth, edit them
  directly) into a `malloc`'d buffer that the owning window's unload frees
  (`free_help_text_buf()`). The three `main.c` texts share one buffer, sized
  for the largest.

## Tests

- **`tests/keys.test.js`** — key namespaces: every watch persist key (scalars,
  alarm/tune/vibration ranges, the keyboard's settings block) is unique and
  disjoint, no `persist_*()` call uses a literal key, and the AppMessage keys are
  unique with ids = 10000 + declaration index (checked against the generated
  `message_keys.auto.c` after a `pebble build`), every key the C code or the phone
  JS uses is declared, Clay-only settings never share a wire key's name, and the
  ids documented in this file match the real ones.
- **`tests/pattern_parity.test.js`** — builds the real watch parser
  (`tests/pattern_parity.c` + `alarm_calc.c`, needs `gcc`) and checks the phone's
  tune/vibration pattern validators (`list_config.ts`) agree with it on a
  hand-written plus a seeded-random corpus (it caught the phone accepting tabs as
  separators; the watch splits on spaces only).
- **`tests/config_page/`** — Playwright (headless Chromium) tests of the
  phone config page: `harness.js` loads the REAL bundled phone JS
  (`build/pebble-js-app.js`) into a Node `vm` with a stubbed `Pebble`/
  `localStorage`, fires `showConfiguration` to get the generated Clay page,
  and feeds the page's Save response back into `webviewclosed` to inspect the
  AppMessages sent. Run `pebble build && npm run test:config` (not part of
  `npm test`; first use needs `npx playwright install chromium`). It already
  caught a real bug: Clay injects component/custom-fn source via
  `String.replace`, so a dollar sign followed by a quote or backtick anywhere
  in those functions — even inside a comment or a `'$'` string — is a special
  replacement pattern that silently corrupts the page. Keep such sequences
  out of `config_*_list.ts`/`config_clay_custom.ts` (use regex literals).

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
- **`tests/functional/`** — emulator-driven functional/E2E tests, a direct
  port of `pebble-another-timer`'s own functional-test framework:
  `tests/functional_framework/` is a git submodule
  (`truhanen/pebble-functional-test-framework`, the same one timer uses),
  consumed via `tests/functional/app.conf` (this app's own UUID/platform/
  container settings) and `tests/functional/run_sequence.sh` (a thin shim
  pinning `--conf`). Scenarios are plain-text `.seq` files under
  `sequences/walkthroughs/` (40 of them, covering the main list, the "+ New
  alarm" wizard, every field editor, cron alarms, the Signal submenu/
  pattern pickers, the custom-tune library, ring/snooze/dismiss (incl. all
  3 `auto_dismiss_mode` values and multi-alarm chaining)) — each one drives real button presses and
  `APP_TEST_HOOKS` AppMessages against the real `pebble-tool` QEMU
  emulator and captures screenshots at meaningful checkpoints. Golden-file
  (approval-based) screenshot/log diffing against `tests/functional/golden/`
  is the regression-test mechanism; see `make build_functional_test_image`/
  `test_functional`/`test_functional_golden`/`update_functional_golden` in
  the Makefile, and `tests/functional_framework/README.md` for the full
  DSL/golden-testing reference.
  - **No new `APP_TEST_HOOKS` keys were added for this** (unlike timer's
    own `TestSetClockDisplay`/`TestSetTimerRemainingDisplay` display-
    override family). Instead: the framework's container runs under a
    pinned-then-ticking-forward virtual clock (`libfaketime`, baked into
    `tests/functional_framework/container/entrypoint.sh`, not something
    this app's own conf configures) for *behavioral* determinism (today-
    vs-tomorrow scheduling decisions, which cron tier a pattern lands in).
  - **Every live-clock-showing screenshot masks just its own clock digits**,
    via that one `SCREENSHOT` instruction's own `--mask-rect` right in the
    `.seq` file — not a whole-run default. Two geometries are in play:
    `--mask-rect 70x25+0+203` for the main window's bottom bar (also drawn
    by the time/cron editors, the edit menu, and the Signal submenu — see
    the "bottom bar isn't main-window-only" design point above, including
    its own Signal-submenu correction) and `--mask-rect 70x30+0+198` for the
    ring screen's own, differently-positioned current-time text (a separate
    window, own geometry — `ALARM_CLOCK_LEFT_MARGIN`/`s_alarm_time`'s own
    `GRect`). Neither masks the bottom bar's `"Next: ..."` side, which
    several sequences genuinely assert on. A whole-run default (one
    `--mask-rect` applied to every screenshot via `tests/functional/
    run_sequence.sh` and the Makefile, requiring a small local patch to
    `run_batch.sh` to even forward the flag) was tried first and
    deliberately reverted in favor of this — it over-masked every
    screenshot that shows neither live display at all, and the one time it
    "fixed" the ring screen too, that was pure coincidence of the two
    regions' pixel ranges overlapping on this display size, not a real,
    intentional fix (confirmed by actually computing the ring screen's own
    text geometry afterward and finding it only partially covered anyway).
    This is a real, confirmed-the-hard-way fix, not a preemptive one: a
    handful of otherwise-correct, already-fixed sequences started failing
    golden comparison purely from a screenshot landing one real minute
    later than the golden was captured at (`"12:00"` vs. `"12:01"`, a
    71-pixel mismatch confined to the last digit) — ordinary run-to-run
    timing variance, not a regression.
  - **Typing on this device is TOUCH-only** — a real, live-verified finding
    worth knowing before writing or trusting any sequence that opens the
    multitap keyboard (label entry, cron field entry, vibe/sound pattern
    entry): the keyboard's own physical-button click config
    (`multitap_keyboard_window.c`) has SELECT always submit the keyboard's
    *current* text immediately, UP open its settings menu, and DOWN cycle
    page/hold-to-backspace — there is no button-driven "type this
    highlighted key" path at all, unlike what a cursory reading of the
    pattern-entry design notes above might suggest. Actual letter/digit/
    note entry only happens via `TOUCH` on the on-screen grid (this is
    `emery`/Pebble Time 2, which has a real touchscreen) — container-only
    in this framework (see `container/README.md`'s TOUCH section), so a
    sequence that needs to type something either computes `TOUCH`
    coordinates from `multitap_keyboard.c`'s own grid geometry constants
    (`keyboard_label_touch_entry.seq`/`keyboard_long_press_digit_insert.seq`
    are the worked examples) or uses a `Test*` hook to set the resulting
    text/pattern directly and only exercises the surrounding UI mechanics
    with real button presses (most of the Signal/pattern/tune-library
    sequences take this second approach).
  - Live-verifying these sequences also surfaced two stale spots this pass
    fixed directly rather than just noting here: the Signal-submenu design
    point above used to say only Sound's presets commit immediately
    (Vibration's do too, on the same "on request" basis — see that point's
    own text now), and two of the Test hooks table's own example patterns
    above (`TestAlarmVibeCustomPattern`/`TestTuneCustomPattern`) were still
    showing the OLD pre-`STORE_SCHEMA`-20 digit+dash/note-then-digit
    grammar, which no longer parses under the current ABC notation. One
    more finding that's just a fact to know, not a doc fix: a saved custom
    tune's own id allocation (`store_alloc_tune_id`) starts at **1**, not 0.
