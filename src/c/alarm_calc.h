// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_ALARMS 16
#define NAME_LEN   31

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
