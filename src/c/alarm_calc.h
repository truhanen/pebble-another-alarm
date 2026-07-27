// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_ALARMS 16
#define NAME_LEN   31
#define CRON_FIELD_LEN 28   // 27 usable chars + NUL; fits comma-lists like "0-59/5,10,20-25/3"

// Weekday bitmask, matching struct tm.tm_wday convention (0=Sunday..6=Saturday).
#define AC_DAY_SUN (1u << 0)
#define AC_DAY_MON (1u << 1)
#define AC_DAY_TUE (1u << 2)
#define AC_DAY_WED (1u << 3)
#define AC_DAY_THU (1u << 4)
#define AC_DAY_FRI (1u << 5)
#define AC_DAY_SAT (1u << 6)
#define AC_DAY_ALL 0x7Fu
#define AC_DAY_BIT(wday) (1u << (wday))

typedef struct {
  uint32_t id;             // stable identity, 0 = none assigned yet
  char name[NAME_LEN + 1];
  uint8_t hour, minute;     // local time of day
  bool repeats;             // true = fires weekly forever on repeat_days; false = fires once
  uint8_t repeat_days;      // AC_DAY_* bitmask. When !repeats: 0 = next occurrence of the
                            // time (today/tomorrow), or a single bit = fire once on that
                            // specific weekday. When repeats: the weekly recurrence days.
  bool enabled;
  bool skip_next;           // skip just the next occurrence (repeats only)
  uint16_t snooze_minutes;
  uint8_t snooze_max;       // 0 = unlimited
  bool vibration_enabled;
  bool sound_enabled;
  bool auto_stop;            // ring screen fires vibration/sound (per the two
                             // toggles above) exactly once instead of the
                             // usual repeating buzz, then auto-dismisses
                             // itself after a couple seconds -- same end
                             // state as the user pressing Stop. See main.c's
                             // trigger_alarm()/alarm_auto_stop_cb().
  bool timeline_pin_enabled; // reserved placeholder; no push logic exists yet
  bool alarm_pending;        // due, still owed a ring screen
  int64_t snooze_until;      // epoch seconds; 0 = not currently snoozed. Persisted
                             // (not just in-memory) because a wakeup fully relaunches
                             // the app — an in-memory-only deadline would be wiped by
                             // init() before it could ever be checked against "now".
  uint8_t snooze_count;      // snoozes used since this occurrence became due; reset
                             // by ac_mark_fired, not on every app restart.
  int32_t last_fired_day;    // caller-defined "day id" (see ac_is_due) this alarm's
                             // regular occurrence last became due on; -1 = never.
                             // Persisted so a repeating alarm doesn't re-trigger
                             // every time the app is merely reopened later the
                             // same day, after having already been stopped.

  // ---- cron mode (see ac_cron_* below) ----
  // A third schedule shape, alongside the hour/minute+repeat_days fields
  // above: hour/minute/day-of-week each given as an independent cron-style
  // field ("*", "12", "1-2", "*/20", "4-45/10", comma-lists of these),
  // matched every minute rather than fired at most once per day. When
  // is_cron, `repeats`/`skip_next` are unused (forced false/ignored) and
  // `repeat_days` is REPURPOSED to hold the parsed day-of-week mask (same
  // AC_DAY_* bits) instead of the legacy weekly-repeat days — no separate
  // field needed since it's exactly the same bitmask shape either way.
  bool is_cron;
  uint64_t cron_min_mask;    // bit i (0-59) set => minute i matches
  uint32_t cron_hour_mask;   // bit i (0-23) set => hour i matches
  int32_t cron_last_fired_min; // opaque "epoch minutes" (now_s()/60, caller-
                             // computed) this alarm's cron pattern last fired
                             // at; -1 = never. Minute-granularity sibling of
                             // last_fired_day: day granularity can't stop
                             // re-firing the same matched minute across
                             // repeated sweeps within that minute, but must
                             // not block a LATER match the same day — a cron
                             // alarm can genuinely fire many times a day.
  char cron_min[CRON_FIELD_LEN];   // raw text, e.g. "*/20" — for redisplay/re-editing
  char cron_hour[CRON_FIELD_LEN];  // e.g. "9-17/2"
  char cron_dow[CRON_FIELD_LEN];   // e.g. "1-5" — parses into repeat_days above

  // Appended at the very end deliberately -- see STORE_SCHEMA's comment in
  // alarm_store.h. Every field added from here on must go at the end too,
  // never inserted earlier in the struct.
  uint8_t vibe_pattern;      // 0=Double, 1=Short, 2=Long -- own copy per alarm,
                             // seeded from the phone-configured default at
                             // creation (see s_default_vibe_pattern, main.c);
                             // editing it later doesn't touch other alarms
  bool increasing_volume;   // sound steps up per buzz cycle (0, 0, 1, 1, 5,
                             // 10, then +10 each cycle) up to the configured
                             // global AudioVolume, instead of starting at
                             // that volume outright -- own copy per alarm,
                             // seeded from the phone-configured default at
                             // creation (see s_default_increasing_volume,
                             // main.c). See alarm_current_volume() in main.c.
} Alarm;

// Day offset (0..14) from `now` to alarm `a`'s next occurrence, at its own
// hour:minute. Returns -1 if `a` is disabled. Not repeating: offset 0 (today,
// not yet passed) or 1 (tomorrow) when repeat_days==0; when repeat_days has a
// day picked, the offset of that weekday's next occurrence. Repeating: scans
// repeat_days from offset 0..7 (7 covers "today is the only set day and it
// already passed"); if skip_next, the first hit found is skipped and the
// scan continues up to 7 more days out (worst case offset 14).
int ac_next_offset_days(const Alarm *a, int now_wday, int now_hour, int now_min);

// True if alarm `a`'s regular (non-snoozed) occurrence has arrived as of
// `now` — i.e. its time-of-day has passed today AND (it has no specific day
// requirement, or today is one of its days) AND it hasn't already fired on
// `today_day_id` yet. Distinct from ac_next_offset_days, which always
// reports the *next future* occurrence and so deliberately skips "today,
// already passed" — exactly the instant this function needs to detect.
//
// `today_day_id` is an opaque, caller-computed value that only needs to
// differ between calendar days and be stable within one (e.g. days since
// some epoch, or year*400+day-of-year) — alarm_calc.c has no <time.h>
// dependency, so callers own turning wall-clock time into this id. Without
// this check, a repeating alarm whose time has passed would stay "due"
// for the rest of the day: alarm_pending alone isn't enough to prevent a
// re-ring, since stopping the alarm clears alarm_pending, and merely
// reopening the app re-runs this check.
bool ac_is_due(const Alarm *a, int now_wday, int now_hour, int now_min, int32_t today_day_id);

// Picks the alarm (among `count`) with the soonest next occurrence, measured
// in minutes from `now` (now_wday/now_hour/now_min are local wall-clock
// fields). Returns minutes-from-now (>=0) and sets *out_idx, or returns -1
// and leaves *out_idx unset if no alarm is enabled.
int ac_next_occurrence(const Alarm *a, int count, int now_wday, int now_hour, int now_min, int *out_idx);

// Called exactly once, when alarm `a`'s REGULAR (non-snoozed) occurrence
// becomes due: non-repeating alarms auto-disable (not delete) and have their
// repeat_days cleared back to 0 — the specific-weekday designation, if any,
// is now spent, so a later re-enable defaults to "next occurrence of this
// time" instead of staying pinned to the day it already fired on (which
// could otherwise wait up to a week to fire again); a set skip_next on a
// repeating alarm is consumed (cleared); snooze_count resets to 0 (this is
// "the moment the next scheduled occurrence fires" the snooze counter is
// documented to reset on — not any fixed wall-clock boundary, and not on
// every app restart). Repeating alarms with no skip_next are otherwise left
// unchanged (they keep firing on schedule). `today_day_id` (same id space as
// ac_is_due) is stamped onto last_fired_day so ac_is_due won't re-arm this
// same occurrence again later today.
void ac_mark_fired(Alarm *a, int32_t today_day_id);

// "H:MM AM/PM" or "HH:MM", depending on use_24h. Writes into buf (size n).
void ac_format_time(char *buf, size_t n, uint8_t hour, uint8_t minute, bool use_24h);

// Not repeating: "Once" (no day picked) or "Once (Fri)" (a specific weekday
// picked). Repeating: "Every day" (all 7 bits set) or an abbreviated,
// comma-separated day list ("Mon, Wed, Fri") starting the enumeration at
// `first_day_of_week` (0=Sunday, 1=Monday) and wrapping. Writes into buf.
void ac_format_repeat_summary(char *buf, size_t n, bool repeats, uint8_t repeat_days, int first_day_of_week);

// ================================= cron mode =================================

// Parses one cron field's text into a bitmask over [min_val, max_val]
// (minute: 0-59, hour: 0-23, dow: 0-6, matching AC_DAY_BIT). Comma-separated
// list of tokens, each "*" (all values in range), "*/N" (every Nth value
// starting at min_val), "N" (single value), "N-M" (inclusive range, N<=M,
// no wraparound), or "N-M/N2" (stepped range). Returns false (leaving
// *out_mask untouched) if ANY token is malformed: non-numeric, out of
// [min_val,max_val], N>M, a step <= 0, an empty field/token, or trailing
// garbage — all-or-nothing per field, never a partially-applied mask.
bool ac_cron_parse_field(const char *text, int min_val, int max_val, uint64_t *out_mask);

// Day offset (0..7, same bound as ac_next_offset_days's repeat-day scan) +
// hour/minute of this cron pattern's next match STRICTLY after `now`
// (mirroring ac_next_offset_days always reporting the next FUTURE
// occurrence). dow_mask uses AC_DAY_BIT like repeat_days. Returns -1 (out_hour/
// out_minute left unset) if no bit is set anywhere in any of the three masks
// — shouldn't happen with a validated field (even "*" sets every bit), but
// guarded defensively since a caller could hand in an all-zero mask.
int ac_cron_next_offset_days(uint64_t min_mask, uint32_t hour_mask, uint8_t dow_mask,
                              int now_wday, int now_hour, int now_min,
                              int *out_hour, int *out_minute);

// True if the cron pattern matches the EXACT current (now_wday, now_hour,
// now_min) and hasn't already been marked fired for this exact minute.
// now_epoch_min (caller-computed, e.g. now_s()/60 in main.c) is the minute-
// granularity sibling of ac_is_due's today_day_id — alarm_calc.c stays
// <time.h>-free, main.c owns turning wall-clock time into it. Unlike
// ac_is_due, there is no "already fired today" guard: a cron alarm can fire
// many times a day, so only exact-minute dedup (last_fired_min ==
// now_epoch_min) blocks a re-trigger, never a whole day.
bool ac_cron_is_due(uint64_t min_mask, uint32_t hour_mask, uint8_t dow_mask, bool enabled,
                     int now_wday, int now_hour, int now_min,
                     int32_t now_epoch_min, int32_t last_fired_min);

// Space-joined raw cron field strings, e.g. "0-59/20 * 1-5" — truncated by
// the caller's own draw call (GTextOverflowModeTrailingEllipsis), same as
// every other ac_format_* helper. Writes into buf (size n).
void ac_format_cron_summary(char *buf, size_t n, const char *min_str, const char *hour_str, const char *dow_str);
