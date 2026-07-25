// SPDX-License-Identifier: GPL-3.0-only
#include "alarm_calc.h"
#include <stdio.h>

int ac_next_offset_days(const Alarm *a, int now_wday, int now_hour, int now_min) {
  if (!a->enabled) { return -1; }
  int now_total = now_hour * 60 + now_min;
  int a_total = a->hour * 60 + a->minute;

  if (!a->repeats && a->repeat_days == 0) {
    // Not repeating, no specific day picked: today if its time hasn't passed
    // yet, else tomorrow. skip_next isn't meaningful here (auto-disables
    // once it fires).
    return (a_total > now_total) ? 0 : 1;
  }

  // Either repeating, or a one-time alarm targeting a specific weekday:
  // both cases scan repeat_days the same way. Scan offsets 0..7 for a set
  // day; day 0 (today) only counts if its time-of-day hasn't already
  // passed. Offset 7 (today's weekday again, one week later) covers the
  // case where today is the only set day and it's already passed.
  int first_hit = -1;
  for (int off = 0; off <= 7; off++) {
    int wday = (now_wday + off) % 7;
    if (!(a->repeat_days & AC_DAY_BIT(wday))) { continue; }
    if (off == 0 && a_total <= now_total) { continue; }
    first_hit = off;
    break;
  }
  if (first_hit < 0) { return -1; }   // unreachable when repeat_days != 0
  if (!a->repeats || !a->skip_next) { return first_hit; }

  // skip_next: the occurrence at first_hit is skipped; find the next one.
  for (int off = first_hit + 1; off <= first_hit + 7; off++) {
    int wday = (now_wday + off) % 7;
    if (a->repeat_days & AC_DAY_BIT(wday)) { return off; }
  }
  return first_hit;   // unreachable: repeat_days != 0 guarantees a hit within 7 days
}

bool ac_is_due(const Alarm *a, int now_wday, int now_hour, int now_min, int32_t today_day_id) {
  if (!a->enabled) { return false; }
  if (a->last_fired_day == today_day_id) { return false; }   // already fired today
  int now_total = now_hour * 60 + now_min;
  int a_total = a->hour * 60 + a->minute;
  if (a_total > now_total) { return false; }   // time-of-day hasn't arrived yet today
  if (a->repeat_days == 0) { return true; }    // no specific day required
  return (a->repeat_days & AC_DAY_BIT(now_wday)) != 0;
}

int ac_next_occurrence(const Alarm *a, int count, int now_wday, int now_hour, int now_min, int *out_idx) {
  int now_total = now_hour * 60 + now_min;
  bool found = false;
  int best = 0, best_idx = -1;
  for (int i = 0; i < count; i++) {
    int off = ac_next_offset_days(&a[i], now_wday, now_hour, now_min);
    if (off < 0) { continue; }
    int a_total = a[i].hour * 60 + a[i].minute;
    int minutes_from_now = off * 1440 + a_total - now_total;
    if (!found || minutes_from_now < best) { best = minutes_from_now; best_idx = i; found = true; }
  }
  if (!found) { return -1; }
  if (out_idx) { *out_idx = best_idx; }
  return best;
}

void ac_mark_fired(Alarm *a, int32_t today_day_id) {
  a->last_fired_day = today_day_id;
  if (!a->repeats) {
    // The specific-day designation (if any) is now spent: clear it so a
    // later re-enable defaults to "next occurrence of this time" instead of
    // staying pinned to the day it already fired on, which could otherwise
    // wait up to a week to fire again and look like it never fires.
    a->enabled = false;
    a->repeat_days = 0;
  } else if (a->skip_next) {
    a->skip_next = false;
  }
  a->snooze_count = 0;
}

void ac_format_time(char *buf, size_t n, uint8_t hour, uint8_t minute, bool use_24h) {
  if (use_24h) {
    snprintf(buf, n, "%02d:%02d", hour, minute);
  } else {
    int h12 = hour % 12;
    if (h12 == 0) { h12 = 12; }
    snprintf(buf, n, "%d:%02d %s", h12, minute, hour < 12 ? "AM" : "PM");
  }
}

static const char *const DAY_ABBR[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

void ac_format_repeat_summary(char *buf, size_t n, bool repeats, uint8_t repeat_days, int first_day_of_week) {
  if (n == 0) { return; }
  if (!repeats) {
    if (repeat_days == 0) { snprintf(buf, n, "Once"); return; }
    for (int i = 0; i < 7; i++) {
      int wday = (first_day_of_week + i) % 7;
      if (repeat_days & AC_DAY_BIT(wday)) { snprintf(buf, n, "Once (%s)", DAY_ABBR[wday]); return; }
    }
    snprintf(buf, n, "Once");
    return;
  }
  if (repeat_days == 0) { snprintf(buf, n, "Once"); return; }   // degenerate: repeating with no days picked
  if (repeat_days == AC_DAY_ALL) { snprintf(buf, n, "Every day"); return; }
  buf[0] = '\0';
  size_t used = 0;
  for (int i = 0; i < 7; i++) {
    int wday = (first_day_of_week + i) % 7;
    if (!(repeat_days & AC_DAY_BIT(wday))) { continue; }
    if (used >= n) { break; }
    int written = snprintf(buf + used, n - used, "%s%s", used ? ", " : "", DAY_ABBR[wday]);
    if (written < 0) { break; }
    used += (size_t)written;
  }
}

// ================================= cron mode =================================

// Parses an unsigned decimal integer starting at *p, advancing *p past the
// digits consumed. Returns true and sets *out on success (at least one
// digit); false (leaving *p unchanged) if *p doesn't start with a digit.
// Naturally stops at any non-digit (comma, '-', '/', NUL), so callers don't
// need to separately bound it to a token's end.
static bool cron_parse_uint(const char **p, int *out) {
  const char *s = *p;
  if (*s < '0' || *s > '9') { return false; }
  int v = 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    s++;
  }
  *out = v;
  *p = s;
  return true;
}

// Parses one already-isolated (no commas) token — "*", "*/N", "N", "N-M", or
// "N-M/N2" — spanning exactly [tok, tok+len), OR-ing its matches into *mask.
// Returns false on any malformed syntax (out-of-range value, N>M, step<=0,
// trailing garbage that doesn't reach the token's end, empty token).
static bool cron_parse_token(const char *tok, int len, int min_val, int max_val, uint64_t *mask) {
  if (len <= 0) { return false; }
  const char *p = tok;
  const char *end = tok + len;

  int range_lo, range_hi;
  if (*p == '*') {
    p++;
    range_lo = min_val; range_hi = max_val;
  } else {
    if (!cron_parse_uint(&p, &range_lo)) { return false; }
    if (range_lo < min_val || range_lo > max_val) { return false; }
    range_hi = range_lo;
    if (p < end && *p == '-') {
      p++;
      if (!cron_parse_uint(&p, &range_hi)) { return false; }
      if (range_hi < min_val || range_hi > max_val || range_hi < range_lo) { return false; }
    }
  }
  int step = 1;
  if (p < end && *p == '/') {
    p++;
    if (!cron_parse_uint(&p, &step)) { return false; }
    if (step <= 0) { return false; }
  }
  if (p != end) { return false; }   // trailing garbage that never reached the token's end

  for (int v = range_lo; v <= range_hi; v += step) {
    *mask |= (1ULL << v);
  }
  return true;
}

bool ac_cron_parse_field(const char *text, int min_val, int max_val, uint64_t *out_mask) {
  if (!text || !*text) { return false; }
  uint64_t mask = 0;
  const char *p = text;
  while (1) {
    const char *tok_start = p;
    while (*p && *p != ',') { p++; }
    if (!cron_parse_token(tok_start, (int)(p - tok_start), min_val, max_val, &mask)) { return false; }
    if (!*p) { break; }
    p++;   // skip the comma; the next loop iteration's len==0 check catches a trailing comma
  }
  *out_mask = mask;
  return true;
}

int ac_cron_next_offset_days(uint64_t min_mask, uint32_t hour_mask, uint8_t dow_mask,
                              int now_wday, int now_hour, int now_min,
                              int *out_hour, int *out_minute) {
  if (min_mask == 0 || hour_mask == 0 || dow_mask == 0) { return -1; }
  for (int off = 0; off <= 7; off++) {
    int wday = (now_wday + off) % 7;
    if (!(dow_mask & AC_DAY_BIT(wday))) { continue; }
    int start_hour = (off == 0) ? now_hour : 0;
    for (int hour = start_hour; hour <= 23; hour++) {
      if (!(hour_mask & (1u << hour))) { continue; }
      int start_min = (off == 0 && hour == now_hour) ? now_min + 1 : 0;
      for (int minute = start_min; minute <= 59; minute++) {
        if (min_mask & (1ULL << minute)) {
          if (out_hour) { *out_hour = hour; }
          if (out_minute) { *out_minute = minute; }
          return off;
        }
      }
    }
  }
  return -1;
}

bool ac_cron_is_due(uint64_t min_mask, uint32_t hour_mask, uint8_t dow_mask, bool enabled,
                     int now_wday, int now_hour, int now_min,
                     int32_t now_epoch_min, int32_t last_fired_min) {
  if (!enabled) { return false; }
  if (last_fired_min == now_epoch_min) { return false; }   // already handled this exact minute
  if (!(dow_mask & AC_DAY_BIT(now_wday))) { return false; }
  if (!(hour_mask & (1u << now_hour))) { return false; }
  if (!(min_mask & (1ULL << now_min))) { return false; }
  return true;
}

void ac_format_cron_summary(char *buf, size_t n, const char *min_str, const char *hour_str, const char *dow_str) {
  snprintf(buf, n, "%s %s %s", min_str ? min_str : "*", hour_str ? hour_str : "*", dow_str ? dow_str : "*");
}
