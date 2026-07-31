// SPDX-License-Identifier: GPL-3.0-only
// Plain-assert host test for alarm_calc.c, no Pebble SDK needed:
//   gcc -I src/c tests/test_alarm_calc.c src/c/alarm_calc.c -o /tmp/t && /tmp/t
#include "alarm_calc.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

// Arbitrary fixed "day id" standing in for "today" throughout these tests
// (ac_is_due/ac_mark_fired treat it as opaque — see alarm_calc.h).
#define TODAY 100

static Alarm mk(uint8_t hour, uint8_t minute, bool repeats, uint8_t repeat_days) {
  Alarm a;
  memset(&a, 0, sizeof(a));
  a.hour = hour;
  a.minute = minute;
  a.repeats = repeats;
  a.repeat_days = repeat_days;
  a.enabled = true;
  a.last_fired_day = -1;   // never fired
  return a;
}

static void test_disabled(void) {
  Alarm a = mk(7, 0, false, 0);
  a.enabled = false;
  assert(ac_next_offset_days(&a, 3, 6, 0) == -1);
}

static void test_one_time_today(void) {
  Alarm a = mk(7, 0, false, 0);
  assert(ac_next_offset_days(&a, 3 /* Wed */, 6, 30) == 0);   // 6:30 < 7:00 -> today
}

static void test_one_time_tomorrow(void) {
  Alarm a = mk(7, 0, false, 0);
  assert(ac_next_offset_days(&a, 3, 7, 30) == 1);    // 7:30 > 7:00 -> already passed, tomorrow
  assert(ac_next_offset_days(&a, 3, 7, 0) == 1);      // exact match counts as "passed" (a_total > now_total is false)
}

// A one-time alarm can also target a specific weekday (not just today/tomorrow) —
// e.g. "fire once, next Friday" — by having repeat_days carry a single bit
// while repeats is false. Scanning logic is identical to the repeating case;
// only ac_mark_fired's post-fire behavior (disable vs. keep recurring) differs.
static void test_one_time_specific_day_not_yet_passed(void) {
  Alarm a = mk(7, 0, false, AC_DAY_WED);
  assert(ac_next_offset_days(&a, 3 /* Wed */, 6, 0) == 0);
}

static void test_one_time_specific_day_passed_waits_a_week(void) {
  Alarm a = mk(7, 0, false, AC_DAY_WED);
  assert(ac_next_offset_days(&a, 3, 8, 0) == 7);
}

static void test_one_time_specific_day_later_this_week(void) {
  Alarm a = mk(7, 0, false, AC_DAY_FRI);
  assert(ac_next_offset_days(&a, 3 /* Wed */, 6, 0) == 2);   // Wed -> Fri
}

static void test_repeating_today_not_passed(void) {
  Alarm a = mk(7, 0, true, AC_DAY_WED);
  assert(ac_next_offset_days(&a, 3 /* Wed */, 6, 0) == 0);
}

static void test_repeating_today_passed(void) {
  Alarm a = mk(7, 0, true, AC_DAY_WED);
  // Wed already passed -> next Wed is 7 days out.
  assert(ac_next_offset_days(&a, 3, 8, 0) == 7);
}

static void test_repeating_no_day_this_week_until_later(void) {
  Alarm a = mk(7, 0, true, AC_DAY_FRI);
  assert(ac_next_offset_days(&a, 3 /* Wed */, 6, 0) == 2);   // Wed -> Fri
}

static void test_repeating_wraparound_sat_to_sun(void) {
  Alarm a = mk(7, 0, true, AC_DAY_SUN);
  assert(ac_next_offset_days(&a, 6 /* Sat */, 6, 0) == 1);   // Sat -> Sun tomorrow
}

static void test_skip_next_repeating(void) {
  Alarm a = mk(7, 0, true, AC_DAY_WED);
  a.skip_next = true;
  // Without skip_next, today (Wed) not yet passed would hit at offset 0;
  // with skip_next set, that occurrence is skipped -> next Wed, offset 7.
  assert(ac_next_offset_days(&a, 3, 6, 0) == 7);
}

// skip_next is documented as "repeats only" — a one-time alarm targeting a
// specific day ignores it (there's no "next" occurrence to skip to; it just
// fires on the picked day, same as if skip_next were never set).
static void test_skip_next_ignored_for_one_time(void) {
  Alarm a = mk(7, 0, false, AC_DAY_WED);
  a.skip_next = true;
  assert(ac_next_offset_days(&a, 3, 6, 0) == 0);
}

// ac_is_due is what sweep_due_alarms uses to detect "should this alarm ring
// right now" — deliberately a different question from ac_next_offset_days
// (which always reports the *next future* occurrence and so skips "today,
// already passed"). A regression here previously meant the ring screen/
// vibration never fired at all: sweep_due_alarms used to reuse
// ac_next_offset_days and check `offset == 0`, but for an alarm whose time
// had just passed, the offset is already advanced past today, so it was
// never seen as due.
static void test_is_due_one_time_time_just_passed(void) {
  Alarm a = mk(7, 0, false, 0);
  assert(ac_is_due(&a, 3, 7, 0, TODAY) == true);    // exact minute match
  assert(ac_is_due(&a, 3, 7, 5, TODAY) == true);    // a bit after
  assert(ac_is_due(&a, 3, 6, 59, TODAY) == false);  // not yet
}

static void test_is_due_one_time_specific_day_matches(void) {
  Alarm a = mk(7, 0, false, AC_DAY_FRI);
  assert(ac_is_due(&a, 5 /* Fri */, 7, 0, TODAY) == true);
  assert(ac_is_due(&a, 4 /* Thu */, 7, 0, TODAY) == false);   // right time, wrong day
}

static void test_is_due_repeating_today_matches(void) {
  Alarm a = mk(7, 0, true, AC_DAY_WED);
  assert(ac_is_due(&a, 3 /* Wed */, 7, 0, TODAY) == true);
  assert(ac_is_due(&a, 3, 6, 59, TODAY) == false);
  assert(ac_is_due(&a, 4 /* Thu */, 7, 0, TODAY) == false);
}

static void test_is_due_disabled_never(void) {
  Alarm a = mk(7, 0, false, 0);
  a.enabled = false;
  assert(ac_is_due(&a, 3, 7, 0, TODAY) == false);
}

// Regression: a repeating alarm has no way to tell "already fired, still
// waiting for tomorrow" apart from "time-of-day passed and today matches"
// other than last_fired_day. Without it, stopping the alarm (which clears
// alarm_pending) and simply reopening the app later the same day would mark
// it due all over again, ringing forever until midnight.
static void test_is_due_repeating_already_fired_today_stays_not_due(void) {
  Alarm a = mk(7, 0, true, AC_DAY_WED);
  assert(ac_is_due(&a, 3 /* Wed */, 7, 0, TODAY) == true);
  ac_mark_fired(&a, TODAY);
  assert(ac_is_due(&a, 3, 7, 30, TODAY) == false);   // later the same day: not due again
  assert(ac_is_due(&a, 3, 7, 30, TODAY + 1) == true);   // a new day: due again
}

static void test_next_occurrence_picks_soonest(void) {
  Alarm alarms[2];
  alarms[0] = mk(9, 0, false, 0);   // one-time, later today
  alarms[1] = mk(7, 30, false, 0);  // one-time, sooner today
  int idx = -1;
  int minutes = ac_next_occurrence(alarms, 2, 3, 6, 0, &idx);
  assert(idx == 1);
  assert(minutes == 90);   // 7:30 - 6:00 = 90 min
}

static void test_next_occurrence_none_enabled(void) {
  Alarm alarms[1];
  alarms[0] = mk(9, 0, false, 0);
  alarms[0].enabled = false;
  int idx = 0;
  assert(ac_next_occurrence(alarms, 1, 3, 6, 0, &idx) == -1);
}

static void test_mark_fired_one_time_disables(void) {
  Alarm a = mk(7, 0, false, 0);
  ac_mark_fired(&a, TODAY);
  assert(a.enabled == false);
}

// snooze_count must reset "the moment the next scheduled occurrence fires"
// (not on a fixed wall-clock boundary, and not on every app restart — it's
// persisted precisely so a wakeup relaunch doesn't lose it mid-snooze).
static void test_mark_fired_resets_snooze_count(void) {
  Alarm a = mk(7, 0, true, AC_DAY_MON);
  a.snooze_count = 2;
  ac_mark_fired(&a, TODAY);
  assert(a.snooze_count == 0);
}

static void test_mark_fired_one_time_with_day_also_disables(void) {
  Alarm a = mk(7, 0, false, AC_DAY_FRI);
  ac_mark_fired(&a, TODAY);
  assert(a.enabled == false);
}

// Regression: a one-time alarm targeting a specific weekday (e.g. "fire once
// on Friday") must have that day designation cleared once it fires — else
// re-enabling it later (after just changing the time, not the Repeat
// screen) stays pinned to that stale weekday and can wait up to a week to
// fire again, which looks like it never fires at all.
static void test_mark_fired_one_time_specific_day_clears_repeat_days(void) {
  Alarm a = mk(7, 0, false, AC_DAY_FRI);
  ac_mark_fired(&a, TODAY);
  assert(a.repeat_days == 0);
  // Re-enable with a new time on some other day: should be due today, not
  // stuck waiting for next Friday.
  a.enabled = true;
  a.hour = 8;
  a.minute = 0;
  assert(ac_is_due(&a, 1 /* Mon */, 8, 0, TODAY + 1) == true);
}

static void test_mark_fired_repeating_clears_skip_next(void) {
  Alarm a = mk(7, 0, true, AC_DAY_MON);
  a.skip_next = true;
  ac_mark_fired(&a, TODAY);
  assert(a.enabled == true);
  assert(a.skip_next == false);
}

static void test_mark_fired_repeating_no_skip_next_unchanged(void) {
  Alarm a = mk(7, 0, true, AC_DAY_MON);
  ac_mark_fired(&a, TODAY);
  assert(a.enabled == true);
  assert(a.skip_next == false);
}

static void test_format_time(void) {
  char buf[16];
  ac_format_time(buf, sizeof(buf), 7, 5, true);
  assert(strcmp(buf, "07:05") == 0);
  ac_format_time(buf, sizeof(buf), 0, 0, false);
  assert(strcmp(buf, "12:00 am") == 0);
  ac_format_time(buf, sizeof(buf), 13, 30, false);
  assert(strcmp(buf, "1:30 pm") == 0);
  ac_format_time(buf, sizeof(buf), 12, 0, false);
  assert(strcmp(buf, "12:00 pm") == 0);
}

static void test_format_repeat_summary(void) {
  char buf[64];
  ac_format_repeat_summary(buf, sizeof(buf), false, 0, 1);
  assert(strcmp(buf, "Once") == 0);
  ac_format_repeat_summary(buf, sizeof(buf), false, AC_DAY_FRI, 1);
  assert(strcmp(buf, "Once (Fri)") == 0);
  ac_format_repeat_summary(buf, sizeof(buf), true, AC_DAY_ALL, 1);
  assert(strcmp(buf, "Every day") == 0);
  ac_format_repeat_summary(buf, sizeof(buf), true, AC_DAY_MON | AC_DAY_WED | AC_DAY_FRI, 1 /* Monday first */);
  assert(strcmp(buf, "Mon, Wed, Fri") == 0);
  ac_format_repeat_summary(buf, sizeof(buf), true, AC_DAY_SUN | AC_DAY_SAT, 1 /* Monday first: Sat wraps before Sun */);
  assert(strcmp(buf, "Sat, Sun") == 0);
}

// ================================= cron mode =================================

static void test_cron_parse_field_star(void) {
  uint64_t m = 0;
  assert(ac_cron_parse_field("*", 0, 59, &m));
  for (int i = 0; i <= 59; i++) { assert(m & (1ULL << i)); }
  assert(!(m & (1ULL << 60)));
}

static void test_cron_parse_field_step_from_star(void) {
  uint64_t m = 0;
  assert(ac_cron_parse_field("*/20", 0, 59, &m));
  assert(m == ((1ULL << 0) | (1ULL << 20) | (1ULL << 40)));
}

static void test_cron_parse_field_single(void) {
  uint64_t m = 0;
  assert(ac_cron_parse_field("12", 0, 59, &m));
  assert(m == (1ULL << 12));
}

static void test_cron_parse_field_range(void) {
  uint64_t m = 0;
  assert(ac_cron_parse_field("1-2", 0, 59, &m));
  assert(m == ((1ULL << 1) | (1ULL << 2)));
}

static void test_cron_parse_field_stepped_range(void) {
  uint64_t m = 0;
  assert(ac_cron_parse_field("4-45/10", 0, 59, &m));
  assert(m == ((1ULL << 4) | (1ULL << 14) | (1ULL << 24) | (1ULL << 34) | (1ULL << 44)));
}

static void test_cron_parse_field_comma_union(void) {
  uint64_t m = 0;
  assert(ac_cron_parse_field("1,5,10-15", 0, 59, &m));
  uint64_t expect = (1ULL << 1) | (1ULL << 5);
  for (int i = 10; i <= 15; i++) { expect |= (1ULL << i); }
  assert(m == expect);
}

static void test_cron_parse_field_invalid(void) {
  uint64_t m;
  assert(!ac_cron_parse_field("", 0, 59, &m));
  assert(!ac_cron_parse_field("abc", 0, 59, &m));
  assert(!ac_cron_parse_field("-5", 0, 59, &m));
  assert(!ac_cron_parse_field("60", 0, 59, &m));    // out of range for minute
  assert(!ac_cron_parse_field("5-3", 0, 59, &m));   // reversed range
  assert(!ac_cron_parse_field("*/0", 0, 59, &m));   // zero step
  assert(!ac_cron_parse_field("12x", 0, 59, &m));   // trailing garbage
  assert(!ac_cron_parse_field("1,", 0, 59, &m));    // trailing comma -> empty token
  assert(!ac_cron_parse_field("1,,2", 0, 59, &m));  // empty token in the middle
}

static void test_cron_next_offset_days_matches_now_reports_next_future(void) {
  uint64_t min_all, hour_all;
  assert(ac_cron_parse_field("*", 0, 59, &min_all));
  assert(ac_cron_parse_field("*", 0, 23, &hour_all));
  int oh = -1, om = -1;
  // Every minute matches; "now" itself doesn't count -- next hit is the very next minute.
  int off = ac_cron_next_offset_days(min_all, (uint32_t)hour_all, AC_DAY_ALL, false, 3, 10, 30, &oh, &om);
  assert(off == 0 && oh == 10 && om == 31);
}

static void test_cron_next_offset_days_later_same_day(void) {
  uint64_t m20, hour_all;
  assert(ac_cron_parse_field("*/20", 0, 59, &m20));
  assert(ac_cron_parse_field("*", 0, 23, &hour_all));
  int oh = -1, om = -1;
  int off = ac_cron_next_offset_days(m20, (uint32_t)hour_all, AC_DAY_ALL, false, 3, 10, 5, &oh, &om);
  assert(off == 0 && oh == 10 && om == 20);
  off = ac_cron_next_offset_days(m20, (uint32_t)hour_all, AC_DAY_ALL, false, 3, 10, 45, &oh, &om);
  assert(off == 0 && oh == 11 && om == 0);   // rolls into next matching hour
}

static void test_cron_next_offset_days_tomorrow_when_dow_skips_today(void) {
  uint64_t min_all, hour_all;
  assert(ac_cron_parse_field("*", 0, 59, &min_all));
  assert(ac_cron_parse_field("*", 0, 23, &hour_all));
  int oh = -1, om = -1;
  // Today is Wed (3); only Mon (1) matches -> 5 days out.
  int off = ac_cron_next_offset_days(min_all, (uint32_t)hour_all, AC_DAY_MON, false, 3, 10, 30, &oh, &om);
  assert(off == 5);
}

// Demonstrates multi-fire at the pure-math level: a */20 minute pattern
// produces multiple distinct hits across successive calls as "now" advances,
// unlike a legacy alarm's single fixed hour:minute.
static void test_cron_next_offset_days_multi_fire_progression(void) {
  uint64_t m20, hour_all;
  assert(ac_cron_parse_field("*/20", 0, 59, &m20));
  assert(ac_cron_parse_field("*", 0, 23, &hour_all));
  int oh, om;
  int off = ac_cron_next_offset_days(m20, (uint32_t)hour_all, AC_DAY_ALL, false, 3, 23, 45, &oh, &om);
  assert(off == 1 && oh == 0 && om == 0);
  off = ac_cron_next_offset_days(m20, (uint32_t)hour_all, AC_DAY_ALL, false, 3, 0, 0, &oh, &om);
  assert(off == 0 && oh == 0 && om == 20);
  off = ac_cron_next_offset_days(m20, (uint32_t)hour_all, AC_DAY_ALL, false, 3, 0, 20, &oh, &om);
  assert(off == 0 && oh == 0 && om == 40);
}

// skip_next: the very next match is skipped, reporting the SECOND one
// instead -- mirrors ac_next_offset_days's own skip_next behavior, applied
// to cron's 3-dimensional (day/hour/minute) scan.
static void test_cron_next_offset_days_skip_next_same_day(void) {
  uint64_t m20, hour_all;
  assert(ac_cron_parse_field("*/20", 0, 59, &m20));
  assert(ac_cron_parse_field("*", 0, 23, &hour_all));
  int oh = -1, om = -1;
  // Without skip: next hit after 10:05 is 10:20.
  int off = ac_cron_next_offset_days(m20, (uint32_t)hour_all, AC_DAY_ALL, true, 3, 10, 5, &oh, &om);
  assert(off == 0 && oh == 10 && om == 40);   // 10:20 skipped -> 10:40
}

static void test_cron_next_offset_days_skip_next_across_day_boundary(void) {
  uint64_t m20, hour_all;
  assert(ac_cron_parse_field("*/20", 0, 59, &m20));
  assert(ac_cron_parse_field("*", 0, 23, &hour_all));
  int oh = -1, om = -1;
  // First hit after 23:45 is tomorrow 00:00; skipping it lands on 00:20,
  // still tomorrow (offset 1, not 2) -- the skip must resume the scan from
  // the skipped hit's own day, not from `now`'s day again.
  int off = ac_cron_next_offset_days(m20, (uint32_t)hour_all, AC_DAY_ALL, true, 3, 23, 45, &oh, &om);
  assert(off == 1 && oh == 0 && om == 20);
}

static void test_cron_next_offset_days_skip_next_across_dow_gap(void) {
  // A single weekly slot (10:30 Monday only, not "every minute/hour" like
  // the other tests) so skipping the first hit genuinely has to jump a
  // whole week rather than landing on a later minute the same day.
  uint64_t min30, hour10;
  assert(ac_cron_parse_field("30", 0, 59, &min30));
  assert(ac_cron_parse_field("10", 0, 23, &hour10));
  int oh = -1, om = -1;
  // First hit from Wed 10:30 is next Monday (offset 5); skipping it must
  // land on the Monday AFTER THAT (offset 12), not the very next day.
  int off = ac_cron_next_offset_days(min30, (uint32_t)hour10, AC_DAY_MON, true, 3, 10, 30, &oh, &om);
  assert(off == 12 && oh == 10 && om == 30);
}

static void test_cron_is_due_matches_and_not_yet_fired_this_minute(void) {
  uint64_t m20, hour_all;
  assert(ac_cron_parse_field("*/20", 0, 59, &m20));
  assert(ac_cron_parse_field("*", 0, 23, &hour_all));
  assert(ac_cron_is_due(m20, (uint32_t)hour_all, AC_DAY_ALL, true, 3, 10, 20, 1000, 999) == true);
}

static void test_cron_is_due_already_fired_this_exact_minute(void) {
  uint64_t m20, hour_all;
  assert(ac_cron_parse_field("*/20", 0, 59, &m20));
  assert(ac_cron_parse_field("*", 0, 23, &hour_all));
  assert(ac_cron_is_due(m20, (uint32_t)hour_all, AC_DAY_ALL, true, 3, 10, 20, 1000, 1000) == false);
}

// Multi-fire: an older last_fired_min doesn't block the CURRENT match, unlike
// ac_is_due's whole-day guard.
static void test_cron_is_due_older_last_fired_still_due(void) {
  uint64_t m20, hour_all;
  assert(ac_cron_parse_field("*/20", 0, 59, &m20));
  assert(ac_cron_parse_field("*", 0, 23, &hour_all));
  assert(ac_cron_is_due(m20, (uint32_t)hour_all, AC_DAY_ALL, true, 3, 10, 20, 1000, 980) == true);
}

static void test_cron_is_due_field_mismatch(void) {
  uint64_t m20, hour_all;
  assert(ac_cron_parse_field("*/20", 0, 59, &m20));
  assert(ac_cron_parse_field("*", 0, 23, &hour_all));
  assert(ac_cron_is_due(m20, (uint32_t)hour_all, AC_DAY_ALL, true, 3, 10, 21, 1000, 999) == false);   // minute mismatch
  assert(ac_cron_is_due(m20, (uint32_t)hour_all, AC_DAY_MON, true, 3, 10, 20, 1000, 999) == false);   // dow mismatch
  uint64_t hour9;
  assert(ac_cron_parse_field("9", 0, 23, &hour9));
  assert(ac_cron_is_due(m20, (uint32_t)hour9, AC_DAY_ALL, true, 3, 10, 20, 1000, 999) == false);      // hour mismatch
}

static void test_cron_is_due_disabled_never(void) {
  uint64_t m20, hour_all;
  assert(ac_cron_parse_field("*/20", 0, 59, &m20));
  assert(ac_cron_parse_field("*", 0, 23, &hour_all));
  assert(ac_cron_is_due(m20, (uint32_t)hour_all, AC_DAY_ALL, false, 3, 10, 20, 1000, 999) == false);
}

static void test_format_cron_summary(void) {
  char buf[64];
  ac_format_cron_summary(buf, sizeof(buf), "*/20", "*", "1-5");
  assert(strcmp(buf, "*/20 * 1-5") == 0);
  ac_format_cron_summary(buf, sizeof(buf), NULL, NULL, NULL);
  assert(strcmp(buf, "* * *") == 0);
}

int main(void) {
  test_disabled();
  test_one_time_today();
  test_one_time_tomorrow();
  test_one_time_specific_day_not_yet_passed();
  test_one_time_specific_day_passed_waits_a_week();
  test_one_time_specific_day_later_this_week();
  test_repeating_today_not_passed();
  test_repeating_today_passed();
  test_repeating_no_day_this_week_until_later();
  test_repeating_wraparound_sat_to_sun();
  test_skip_next_repeating();
  test_skip_next_ignored_for_one_time();
  test_is_due_one_time_time_just_passed();
  test_is_due_one_time_specific_day_matches();
  test_is_due_repeating_today_matches();
  test_is_due_disabled_never();
  test_is_due_repeating_already_fired_today_stays_not_due();
  test_next_occurrence_picks_soonest();
  test_next_occurrence_none_enabled();
  test_mark_fired_one_time_disables();
  test_mark_fired_resets_snooze_count();
  test_mark_fired_one_time_with_day_also_disables();
  test_mark_fired_one_time_specific_day_clears_repeat_days();
  test_mark_fired_repeating_clears_skip_next();
  test_mark_fired_repeating_no_skip_next_unchanged();
  test_format_time();
  test_format_repeat_summary();
  test_cron_parse_field_star();
  test_cron_parse_field_step_from_star();
  test_cron_parse_field_single();
  test_cron_parse_field_range();
  test_cron_parse_field_stepped_range();
  test_cron_parse_field_comma_union();
  test_cron_parse_field_invalid();
  test_cron_next_offset_days_matches_now_reports_next_future();
  test_cron_next_offset_days_later_same_day();
  test_cron_next_offset_days_tomorrow_when_dow_skips_today();
  test_cron_next_offset_days_multi_fire_progression();
  test_cron_next_offset_days_skip_next_same_day();
  test_cron_next_offset_days_skip_next_across_day_boundary();
  test_cron_next_offset_days_skip_next_across_dow_gap();
  test_cron_is_due_matches_and_not_yet_fired_this_minute();
  test_cron_is_due_already_fired_this_exact_minute();
  test_cron_is_due_older_last_fired_still_due();
  test_cron_is_due_field_mismatch();
  test_cron_is_due_disabled_never();
  test_format_cron_summary();
  printf("all alarm_calc tests passed\n");
  return 0;
}
