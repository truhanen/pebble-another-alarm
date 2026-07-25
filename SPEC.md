# Another alarm — feature spec

Status: **implemented** — see `CLAUDE.md` for the as-built architecture and
a few implementation-time refinements not reflected below: time/duration
entry uses simple button-driven pickers rather than a ported touch-dial
widget, the "+ New alarm" wizard has no back-navigation across its steps
(only the very first field can abort creation), and the edit menu gained a
"Delete alarm" row (not in the original list below, but necessary — there
was otherwise no way to remove an alarm). This document is kept as the
historical design record. This adapts patterns proven in the sibling
`pebble-another-timer` project wherever they fit; alarms differ from timers
in one structural way timers don't have to deal with — **alarms must fire at
a wall-clock time while the app isn't running**, which timers never needed
since a timer only counts down while already open-ish (via a single armed
wakeup for whichever timer is soonest). Section 3 covers how that reuses and
extends the same mechanism.

## 1. Data model

One `Alarm` struct per configured alarm (cap `MAX_ALARMS`, suggest 16 to
match `timer_calc.h`'s `MAX_TIMERS`):

```c
typedef struct {
  uint32_t id;            // stable id, survives reordering (see timer_calc.c Timer.id)
  char name[NAME_MAX+1];  // label, NAME_MAX=31 to match timer_config
  uint8_t hour, minute;   // local time of day
  uint8_t repeat_days;    // bitmask, bit0=Sunday..bit6=Saturday (0 = one-time)
  bool enabled;
  bool skip_next;         // "disable just the next occurrence" of a repeating alarm
  uint16_t snooze_minutes;
  uint8_t snooze_max;     // 0 = unlimited
  bool vibration_enabled;
  bool sound_enabled;     // gates speaker_play_notes()/speaker_play_tone(), see §7
  bool timeline_pin_enabled; // reserved placeholder, not implemented — see §9
} Alarm;
```

One-time alarms (`repeat_days == 0`) **auto-disable** (not delete) after
firing — kept in the list, greyed out, re-enabled/edited later without
re-entering time/label/snooze. This is a deliberate departure from timer's
delete-on-finish default (decided in §9).

Global settings (phone-configured, §6): first day of week, default vibration
pattern.

## 2. Persistence (`alarm_store.c`/`.h`)

Same shape as `timer_store.c`: one `Alarm` per persist key
(`PERSIST_KEY_ALARM_BASE + i`, packed struct comfortably under the 256B/key
cap), plus scalar keys for schema version, count, wakeup id, first-day-of-week,
default vibe pattern, and a `next_local_id` counter. `skip_next` and the
three booleans (`vibration_enabled`/`sound_enabled`/`timeline_pin_enabled`)
could each live in a `PERSIST_KEY_EPHEMERAL`-style bitmask instead of struct
fields if struct size becomes tight — timer_store.c:`PERSIST_KEY_EPHEMERAL`
is the precedent (one bit per item, cheaper than a whole extra field).
`timeline_pin_enabled` is persisted like any other field even though it's
inert (§9.2) — reserving the bit now avoids a schema migration later if pin
push ever gets built.

## 3. Scheduling (the one truly alarm-specific piece)

Reuse timer's **single-soonest-wakeup** pattern verbatim
(`pebble-another-timer/src/c/main.c:303` `rearm_wakeup()`,
`timer_store.c:38-44` `store_load_wakeup_id`/`store_save_wakeup_id`):

- A pure function `alarm_calc_next_occurrence(alarms, count, now)` (analogous
  to `tc_soonest_end`) computes the next fire time across all *enabled*
  alarms — for repeating alarms, the next matching weekday at `hour:minute`
  on/after `now`; for one-time alarms, its own `hour:minute` on the next
  matching date.
- `rearm_wakeup()` cancels/reschedules exactly one `WakeupId` for that instant,
  using the same "arm new before cancelling old" ordering as the timer app so
  a refused reschedule never leaves the app with zero wakeups armed. Call it
  after every mutation (create/edit/enable/disable/delete/skip-next), exactly
  like the timer app calls it after every state change.
- On launch, check `wakeup_get_launch_event()` (timer's `main.c:2895-2897`
  pattern) to detect a wakeup-triggered launch vs. a manual open, and only
  then push the alarm-ring window immediately.
- Because only **one** wakeup can be armed at a time but *multiple* alarms
  could coincide at the same minute, `rearm_wakeup()`'s ring handler must,
  on firing, check for **all** alarms due at that instant (not just the one
  that "caused" the wakeup) — same as the timer app's `show_next_pending_alarm()`
  (`main.c:814`) sweeping for other timers that finished around the same time.

## 4. Main list view

Single `MenuLayer`, `s_count + 1` rows (trailing bold "+ New alarm" row,
same as `main.c:2086-2108`). Per the notes, each row is **two fixed lines**
(no timer-style inline-expanding detail row — alarms don't need a "running"
sub-state):

- **Line 1**: time (`HH:MM`, bold, fixed-width like timer's time column) +
  label, `GTextOverflowModeTrailingEllipsis` — mirrors
  `pebble-another-timer/src/c/main.c:2205-2235`.
- **Line 2**: repeat summary (`Mon Tue Wed…` / `Once` / `Every day`) + small
  config glyphs for the toggles that are *on* (vibration/sound/pin), drawn
  procedurally the same way timer draws its state icon
  (`ml_draw_state_icon`, `main.c:2009`) — no bitmap assets needed.

Row tint/state: disabled alarms get a dimmed/gray tint, same technique as
`ml_row_colors` (`main.c:2111-2136`) tinting by `TS_IDLE`/`TS_RUNNING`/`TS_PAUSED`.

## 5. Short-press → edit menu

Unlike the timer app (where short-press *runs* an idle timer and long-press
opens the edit menu, `main.c:2318` vs `2346`), an alarm has no "run" action to
give short-press — **short-press opens the edit menu directly**, reusing the
`s_detail_window`/`MenuLayer` window pattern (`main.c:1560-1570`,
`dl_rebuild_actions`) with alarm-specific rows instead of run-control actions:

1. **Disable/Enable** — if `repeat_days != 0` (repeating) and currently
   enabled, prompt "Disable / Skip next occurrence only" (two-button confirm
   window, or a 2-row sub-menu) before toggling; sets `enabled=false` or
   `skip_next=true` respectively. If one-time or already disabled, toggles
   directly, no prompt.
2. **Time** — opens the touch-dial time editor, reusing `dial_touch.c` +
   `open_dial_window` verbatim (H/M fields only, no seconds field — alarms
   don't need second-granularity).
3. **Day & repeat** — a new small screen: "Repeat: Yes / One-time" then, if
   Yes, a 7-item weekday multi-select (checkboxgroup-style row list, one row
   per day toggled by SELECT, styled after `multitap_keyboard`'s row-based
   selection rather than Clay — this needs a new watch-side widget, there's
   no existing analogue in the timer app).
4. **Snooze** — two fields: duration (minutes, reuse the dial-editor pattern
   with only a minutes field) and max count (a simple up/down stepper row,
   0 = unlimited).
5. **Vibration** — enabled/disabled toggle row (flips `vibration_enabled`).
6. **Sound** — enabled/disabled toggle row (flips `sound_enabled`, gates the
   `speaker_play_notes()`/`speaker_play_tone()` call on the ring screen, §7).
7. **Timeline pins** — **deferred, not in this edit menu yet.**
   `timeline_pin_enabled` exists only as a reserved struct/persistence field
   (§1, §9) — no UI row, no push logic, until pin delivery is actually built.

Each row's editor pushes/pops the same way timer's "Edit duration"/"Edit
label" rows do (`main.c:1352-1373`) — reuse the idempotent
"already-on-stack" window-reuse check at `main.c:1752,1778`.

## 6. "+ New alarm" creation flow

Chained steps, each pushing the next on confirm (mirrors timer's
dial→keyboard chain at `main.c:2743-2760` + `dial_confirm()` `main.c:1057`,
extended with two more steps):

**Time → Day/Repeat → Snooze → Label**

- *Time*: touch-dial H/M editor (as in §5.2).
- *Day*: "Repeat: Yes / One-time"; if Yes, the weekday multi-select from §5.3.
- *Snooze*: duration + max count, defaulting to the phone-configured global
  default so most users can just confirm-through.
- *Label*: `multitap_keyboard_window_push_ex` (verbatim reuse, same as
  `open_label_input_for_new_timer`, `main.c:1506-1512`), `text==NULL` cancels
  the whole creation (mirrors timer behavior — decide whether cancel at this
  last step discards the whole new alarm or keeps it unlabeled; timer keeps
  the timer and just skips naming, recommend the same here).

New alarms default `enabled=true` and call `rearm_wakeup()` on completion.

## 7. Alarm ring screen

Full-screen window, same structural approach as timer's `s_alarm_window`
(`main.c:641` background color, big auto-shrinking title font ladder
`alarm_title_font` `main.c:593-610`, `AppTimer`-driven repeat-buzz
`alarm_buzz_start`/`alarm_buzz_cb` `main.c:521-540`), but **different click
config** per the notes:

- **DOWN double-click** → stop (dismiss, cancel any further snoozes for this
  occurrence). Needs `window_multi_click_subscribe(BUTTON_ID_DOWN, 2, 2,
  0, true, handler)` (Pebble's native multi-click debounce) instead of
  timer's single-click DOWN=stop — timer's `alarm_click_config`
  (`main.c:582`) uses `window_single_click_subscribe` for all three buttons;
  this needs the multi-click variant for DOWN specifically, single-click for
  UP.
- **UP single-click** → snooze: reschedule this alarm `snooze_minutes`
  minutes out (respecting `snooze_max`). The per-firing snooze counter is
  **not** persisted across reboots and resets the moment the alarm's *next
  scheduled* occurrence fires (not on any fixed wall-clock boundary) —
  dismiss the ring window, `rearm_wakeup()`.
- **BACK → no-op.** Ringing continues; BACK does not dismiss, snooze, or
  otherwise affect the alarm (deliberately unlike timer's BACK="keep
  ringing in overtime" semantics, `main.c:668` — here it's simply inert).
- Vibration pattern: use whichever `VibePattern` §6/8's `vibration_enabled` +
  phone-configured default pattern resolve to, via `vibes_enqueue_custom_pattern`
  (timer: fixed pattern `main.c:505`; instant-timer: phone-selectable
  Double/Short/Long via `alarmVibePattern`, `instant_timer.c:399-418` —
  **this alarm app should copy instant-timer's selectable-pattern approach**,
  not timer's fixed one, since the phone config note explicitly asks for it).
- Sound: Emery (the only platform this project targets, per `package.json`)
  has a speaker, and SDK 4.9+ exposes a full audio API
  (`developer.repebble.com/docs/c/User_Interface/Speaker/`) — `sound_enabled`
  gates a `speaker_play_notes()` call (a short looping alert melody) or the
  simpler `speaker_play_tone()` (single tone, ≤10,000ms) alongside the vibe
  pattern. Key limits: 256 notes/sequence (`SPEAKER_MAX_NOTES`), 4 parallel
  tracks (`SPEAKER_MAX_TRACKS`), 16KB total sample data
  (`SPEAKER_MAX_SAMPLE_BYTES_TOTAL`). No capability check is needed since
  every target platform has the hardware; `speaker_stop()` on dismiss/snooze
  alongside `vibes_cancel()`.

## 8. Phone config page (Clay)

Extends the current scaffold's single `ExampleSetting` toggle
(`src/ts/config_clay.ts`) with:

- **First day of week** — `radiogroup` or `select`, `messageKey:
  FirstDayOfWeek`, options Sunday/Monday, `defaultValue: '1'` (Monday). Feeds
  the weekday-picker UI in §5.3/§6 (which day is column 0) and the "Mon Tue
  Wed…" ordering on line 2 of each row (§4).
- **Vibration pattern** — copy instant-timer's `select` field exactly
  (`pebble-instant-timer/src/pkjs/config.json:6-13`): `messageKey:
  AlarmVibePattern`, options Double(0)/Short(1)/Long(2), default `'0'`. Watch
  side must apply instant-timer's `- '0'` ASCII-digit unwrap hack
  (`config.c:77`, referencing pebble-dev/clay#28) when reading it back — Clay
  `select` sends the value as a char, not a raw int.
- **Default snooze duration/max** — pre-fills new alarms' snooze fields
  (§6), doesn't affect existing alarms.

No dedicated "sound" Clay field is planned yet beyond the vibration pattern
above — `sound_enabled` (§1, §5.6) is a per-alarm on/off flip; if a sound
*style* picker (analogous to the vibe-pattern `select`) turns out to be
wanted too, add a phone-configurable `AlarmSoundId`/melody choice the same
way, gated by SDK 4.9+ speaker support.

## 9. Decisions

Resolved from the open questions in the previous draft of this spec:

1. **Sound** — Emery has a speaker; SDK 4.9+'s Speaker API
   (`speaker_play_notes`/`speaker_play_tone`, see §7) is the real
   implementation, not a stub. No hardware-capability check is needed since
   this project only targets emery.
2. **Timeline pins** — deferred entirely for now. `timeline_pin_enabled`
   exists only as a reserved data-model/persistence placeholder (§1, §2);
   no edit-menu row, no push logic. Pushing a real Pebble timeline pin
   requires hitting `https://timeline-api.getpebble.com/…` with a
   *server-side* API key registered to the watchapp — there's no
   local/on-device API for a 3rd-party app to create pins itself, and this
   repo has no backend. Revisit if/when a small serverless pusher gets built.
3. **One-time alarm post-fire lifecycle** — **auto-disable**, not delete
   (§1). Kept in the list, greyed out, re-enable/edit later without losing
   the configured time/label/snooze settings.
4. **Snooze-count reset boundary** — resets the moment the alarm's *next
   scheduled* occurrence fires (§7), not on any fixed wall-clock boundary.
5. **BACK button on the ring screen** — no-op (§7); ringing continues
   uninterrupted.

## 10. Suggested build order

1. `alarm_calc.c`/`.h` — pure struct + next-occurrence math + repeat-days
   logic, host-testable exactly like `timer_calc.c` /
   `tests/test_timer_calc.c` (gcc, no Pebble SDK).
2. `alarm_store.c`/`.h` — persistence, mirroring `timer_store.c`.
3. Wakeup scheduling (§3) — the part with no direct timer precedent to copy
   from beyond the mechanism itself; get this right early since everything
   else depends on it firing correctly while the app is closed.
4. Main list view (§4) — static rendering only, no interactions yet.
5. Ring screen (§7) — can be built/tested independently via a debug
   "trigger now" hook (timer app's `SetTimerState` debug backdoor,
   mentioned in its CLAUDE.md, is the precedent for this kind of
   emulator-only testing hook).
6. Edit menu (§5) and creation flow (§6) — the two biggest UI surfaces,
   share the day/repeat/snooze sub-widgets between them.
7. Phone Clay page (§8).
