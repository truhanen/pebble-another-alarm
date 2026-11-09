// SPDX-License-Identifier: GPL-3.0-only
#include "alarm_calc.h"
#include <stdio.h>
#include <string.h>

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
    snprintf(buf, n, "%d:%02d %s", h12, minute, hour < 12 ? "am" : "pm");
  }
}

static const char *const DAY_ABBR[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
// One-letter-per-day grid used for any repeating multi/single-day schedule
// that isn't "every day": position i is the (first_day_of_week + i)'th
// weekday, filled with that day's first letter if selected or '_' if not,
// space-separated -- e.g. Monday-first Mon-Sat is "M T W T F S _", Fri-Sun
// is "_ _ _ _ F S S". Reads unambiguously at a glance without needing
// per-combo special-cased strings (this replaced fixed "Mon-Fri"/"Sat, Sun"
// strings and the generic comma-joined list previously used here).
static const char DAY_LETTER[7] = { 'S', 'M', 'T', 'W', 'T', 'F', 'S' };  // Sun..Sat

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
    char c = (repeat_days & AC_DAY_BIT(wday)) ? DAY_LETTER[wday] : '_';
    if (used >= n) { break; }
    int written = snprintf(buf + used, n - used, "%s%c", i ? " " : "", c);
    if (written < 0) { break; }
    used += (size_t)written;
  }
}

// ============================ custom sound/vibe patterns ============================

// AC_NOTE_RATIO_DIGITS -- see AC_NOTE_RATIO_DIGIT_COUNT's doc comment
// (alarm_calc.h): the only valid digits for either side of a "[N]['/'D]"
// duration ratio.
const uint8_t AC_NOTE_RATIO_DIGITS[AC_NOTE_RATIO_DIGIT_COUNT] = { 1, 2, 3, 4, 5, 6, 7, 8 };

// AC_NOTE_LETTER/AC_NOTE_MIDI -- see AC_NOTE_COUNT's doc comment
// (alarm_calc.h): C major scale starting at MIDI 96 (C7, close to the
// fixed B6/95 pitch every alarm used before custom sound patterns existed).
// Octave/accidental shifts (see AC_NOTE_OCTAVE_*/AC_NOTE_ACCIDENTAL_*) are
// applied on top of these base pitches at parse/format time, not baked
// into separate table entries.
const char AC_NOTE_LETTER[AC_NOTE_COUNT] = { 'C', 'D', 'E', 'F', 'G', 'A', 'B' };
const uint8_t AC_NOTE_MIDI[AC_NOTE_COUNT] = { 96, 98, 100, 101, 103, 105, 107 };

// Parses one side of a duration ratio. Returns false unless `c` is one of
// AC_NOTE_RATIO_DIGITS -- i.e. '1'-'8'; '0' and '9' are plain ASCII digits
// but not valid ratio digits.
static bool pattern_parse_ratio_digit(char c, uint8_t *out_digit) {
  if (c < '0' || c > '9') { return false; }
  uint8_t d = (uint8_t)(c - '0');
  for (int i = 0; i < AC_NOTE_RATIO_DIGIT_COUNT; i++) {
    if (AC_NOTE_RATIO_DIGITS[i] == d) { *out_digit = d; return true; }
  }
  return false;
}

// Looks up `c` in AC_NOTE_LETTER, returning its index, or -1 if not found.
static int pattern_note_index(char c) {
  for (int i = 0; i < AC_NOTE_COUNT; i++) {
    if (AC_NOTE_LETTER[i] == c) { return i; }
  }
  return -1;
}

static uint8_t pattern_gcd(uint8_t a, uint8_t b) {
  while (b != 0) { uint8_t t = b; b = (uint8_t)(a % b); a = t; }
  return a;
}

// Parses the optional "[N]['/'D]" duration-ratio suffix occupying
// tok[start..tlen) -- shared by both grammars: vibe's on-beat token is
// nothing BUT this suffix (start==0), while sound's on-token/rest reach it
// only after consuming their own leading characters. An empty suffix
// (start==tlen) is the "bare" case, N=1,D=1 (AC_NOTE_DEFAULT_MS exactly).
// N/D is reduced to lowest terms by their GCD before packing, so two
// spellings of the same ratio (e.g. "1" and "2/2") always produce the
// identical byte -- see AC_PATTERN_DURATION_MASK's doc comment for why
// this matters. Returns false (leaving *out_payload untouched) if either
// side isn't a valid AC_NOTE_RATIO_DIGITS digit, '/' has no following
// digit, or any character remains unconsumed.
static bool pattern_parse_duration(const char *tok, int start, int tlen, uint8_t *out_payload) {
  uint8_t n = 1, d = 1;
  int i = start;
  if (i < tlen && tok[i] != '/') {
    if (!pattern_parse_ratio_digit(tok[i], &n)) { return false; }
    i++;
  }
  if (i < tlen && tok[i] == '/') {
    i++;
    if (i >= tlen || !pattern_parse_ratio_digit(tok[i], &d)) { return false; }
    i++;
  }
  if (i != tlen) { return false; }   // trailing garbage
  uint8_t g = pattern_gcd(n, d);
  n = (uint8_t)(n / g);
  d = (uint8_t)(d / g);
  *out_payload = (uint8_t)((n - 1) * AC_NOTE_RATIO_DIGIT_COUNT + (d - 1));
  return true;
}

// Writes the canonical duration-ratio text for an already-reduced `payload`
// -- empty string when N==D==1 ("bare"), "N" when D==1, "/D" when N==1,
// else "N/D". Shared by both formatters so ac_vibe_pattern_format/
// ac_sound_pattern_format can never disagree on which of several
// equivalent spellings to emit.
static void pattern_format_duration(char *buf, size_t n, uint8_t payload) {
  uint8_t note_n = (uint8_t)(payload / AC_NOTE_RATIO_DIGIT_COUNT + 1);
  uint8_t note_d = (uint8_t)(payload % AC_NOTE_RATIO_DIGIT_COUNT + 1);
  if (n == 0) { return; }
  if (note_n == 1 && note_d == 1) { buf[0] = '\0'; return; }
  if (note_d == 1) { snprintf(buf, n, "%d", note_n); return; }
  if (note_n == 1) { snprintf(buf, n, "/%d", note_d); return; }
  snprintf(buf, n, "%d/%d", note_n, note_d);
}

uint32_t ac_pattern_duration_ms(uint8_t raw) {
  uint8_t payload = raw & AC_PATTERN_DURATION_MASK;
  uint32_t note_n = payload / AC_NOTE_RATIO_DIGIT_COUNT + 1;
  uint32_t note_d = payload % AC_NOTE_RATIO_DIGIT_COUNT + 1;
  return (AC_NOTE_DEFAULT_MS * note_n + note_d / 2) / note_d;   // round to nearest ms
}

bool ac_vibe_pattern_parse(const char *text, uint8_t max_segments, uint8_t *out, uint8_t *out_len) {
  if (!text) { return false; }
  uint8_t parsed[AC_VIBE_PATTERN_MAX_SEGMENTS];
  uint8_t count = 0;
  const char *p = text;
  while (1) {
    while (*p == ' ') { p++; }   // skip leading/repeated separators
    if (!*p) { break; }
    const char *tok = p;
    while (*p && *p != ' ') { p++; }
    int tlen = (int)(p - tok);
    if (count >= max_segments) { return false; }   // too many segments
    bool is_rest = (tok[0] == 'z');
    uint8_t payload;
    // A rest's duration suffix starts right after 'z' and may be empty
    // (bare); an on-beat token IS the duration suffix in its entirety
    // (start==0) -- since a real token is never zero-length, this always
    // consumes at least one character, so a vibe on-beat can never be the
    // empty/bare duration the way a rest or a sound note letter can.
    if (!pattern_parse_duration(tok, is_rest ? 1 : 0, tlen, &payload)) { return false; }
    parsed[count] = payload | (is_rest ? AC_PATTERN_REST_BIT : 0);
    count++;
  }
  if (count == 0) { return false; }
  memcpy(out, parsed, count);
  *out_len = count;
  return true;
}

void ac_vibe_pattern_format(char *buf, size_t n, const uint8_t *pattern, uint8_t len) {
  if (n == 0) { return; }
  buf[0] = '\0';
  size_t used = 0;
  for (uint8_t i = 0; i < len; i++) {
    bool is_rest = (pattern[i] & AC_PATTERN_REST_BIT) != 0;
    char dur[8];
    pattern_format_duration(dur, sizeof(dur), pattern[i] & AC_PATTERN_DURATION_MASK);
    // Unlike sound (whose note letter anchors a bare duration), a vibe
    // on-beat token IS the duration text in its entirety -- it can never
    // be spelled as an empty string (see ac_vibe_pattern_parse's own doc
    // comment), so an on-beat's otherwise-bare ("") duration must be
    // spelled out explicitly as "1" here, or this would format right back
    // into unparseable text.
    if (!is_rest && dur[0] == '\0') { snprintf(dur, sizeof(dur), "1"); }
    int written = snprintf(buf + used, n - used, "%s%s%s", i ? " " : "", is_rest ? "z" : "", dur);
    if (written < 0 || (size_t)written >= n - used) { break; }
    used += (size_t)written;
  }
}

bool ac_sound_pattern_parse(const char *text, uint8_t max_segments,
                             uint8_t *out_flags, uint8_t *out_notes, uint8_t *out_len) {
  if (!text) { return false; }
  uint8_t flags[AC_SOUND_PATTERN_MAX_SEGMENTS];
  uint8_t notes[AC_SOUND_PATTERN_MAX_SEGMENTS];
  uint8_t count = 0;
  const char *p = text;
  while (1) {
    while (*p == ' ') { p++; }
    if (!*p) { break; }
    const char *tok = p;
    while (*p && *p != ' ') { p++; }
    int tlen = (int)(p - tok);
    if (count >= max_segments) { return false; }

    if (tok[0] == 'z') {
      // A rest has no pitch, so nothing but 'z' plus its optional duration
      // suffix is valid here -- an accidental/letter right after 'z' falls
      // through to pattern_parse_duration below and is rejected there,
      // since neither is a valid ratio-digit or '/'.
      uint8_t payload;
      if (!pattern_parse_duration(tok, 1, tlen, &payload)) { return false; }
      flags[count] = payload | AC_PATTERN_REST_BIT;
      notes[count] = 0;
      count++;
      continue;
    }

    int pos = 0;
    int accidental = AC_NOTE_ACCIDENTAL_NATURAL;
    if (tok[0] == '^') { accidental = AC_NOTE_ACCIDENTAL_SHARP; pos = 1; }
    else if (tok[0] == '_') { accidental = AC_NOTE_ACCIDENTAL_FLAT; pos = 1; }

    if (pos >= tlen) { return false; }   // an accidental with no letter, or an empty token
    int ni = pattern_note_index(tok[pos]);
    if (ni < 0) { return false; }   // unrecognized note letter
    pos++;

    // Real ABC-notation octave markers, right after the letter: one or
    // more "'" (apostrophe) shift up an octave each, one or more ","
    // (comma) shift down an octave each -- but never mixed within one
    // token, and each direction has its own cap (see AC_NOTE_OCTAVE_
    // MAX_UP/_MAX_DOWN's doc comment, alarm_calc.h, for why the range is
    // asymmetric). A stray opposite-kind marker, or a run longer than the
    // cap, falls through to pattern_parse_duration below and is rejected
    // there as an unparseable duration.
    int octave = AC_NOTE_OCTAVE_SAME;
    if (pos < tlen && (tok[pos] == '\'' || tok[pos] == ',')) {
      char marker = tok[pos];
      int mcount = 0;
      while (pos < tlen && tok[pos] == marker) { mcount++; pos++; }
      int max_count = (marker == '\'') ? AC_NOTE_OCTAVE_MAX_UP : AC_NOTE_OCTAVE_MAX_DOWN;
      if (mcount > max_count) { return false; }
      octave = (marker == '\'') ? AC_NOTE_OCTAVE_SAME + mcount : AC_NOTE_OCTAVE_SAME - mcount;
    }

    uint8_t payload;
    if (!pattern_parse_duration(tok, pos, tlen, &payload)) { return false; }

    flags[count] = payload;
    notes[count] = (uint8_t)((octave * AC_NOTE_COUNT + ni) * AC_NOTE_ACCIDENTAL_COUNT + accidental);
    count++;
  }
  if (count == 0) { return false; }
  memcpy(out_flags, flags, count);
  memcpy(out_notes, notes, count);
  *out_len = count;
  return true;
}

void ac_sound_pattern_format(char *buf, size_t n, const uint8_t *flags, const uint8_t *notes, uint8_t len) {
  if (n == 0) { return; }
  buf[0] = '\0';
  size_t used = 0;
  for (uint8_t i = 0; i < len; i++) {
    bool is_rest = (flags[i] & AC_PATTERN_REST_BIT) != 0;
    char dur[8];
    pattern_format_duration(dur, sizeof(dur), flags[i] & AC_PATTERN_DURATION_MASK);
    int written;
    if (is_rest) {
      written = snprintf(buf + used, n - used, "%sz%s", i ? " " : "", dur);
    } else {
      int combined = notes[i] / AC_NOTE_ACCIDENTAL_COUNT;
      int accidental = notes[i] % AC_NOTE_ACCIDENTAL_COUNT;
      int octave = combined / AC_NOTE_COUNT;
      int ni = combined % AC_NOTE_COUNT;
      char letter = (ni < AC_NOTE_COUNT) ? AC_NOTE_LETTER[ni] : '?';
      const char *acc_str = (accidental == AC_NOTE_ACCIDENTAL_SHARP) ? "^"
                           : (accidental == AC_NOTE_ACCIDENTAL_FLAT) ? "_" : "";
      // Emit N repeated marker characters for a shift of magnitude N,
      // rather than a single fixed marker -- the exact inverse of parse's
      // marker-run counting above. AC_NOTE_OCTAVE_MAX_DOWN+1 is generous
      // headroom for the marker run plus its NUL.
      char markers[AC_NOTE_OCTAVE_MAX_DOWN + 1];
      int shift = octave - AC_NOTE_OCTAVE_SAME;
      char mchar = (shift > 0) ? '\'' : ',';
      int mcount = (shift > 0) ? shift : -shift;
      if (mcount > AC_NOTE_OCTAVE_MAX_DOWN) { mcount = AC_NOTE_OCTAVE_MAX_DOWN; }   // defensive; shouldn't happen
      for (int m = 0; m < mcount; m++) { markers[m] = mchar; }
      markers[mcount] = '\0';
      written = snprintf(buf + used, n - used, "%s%s%c%s%s", i ? " " : "", acc_str, letter, markers, dur);
    }
    if (written < 0 || (size_t)written >= n - used) { break; }
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

static bool ac_is_leap_year(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int ac_days_in_month(int year, int month) {
  static const int d[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (month == 2 && ac_is_leap_year(year)) { return 29; }
  return d[month];
}

// Sakamoto's algorithm: 0=Sunday..6=Saturday, matching AC_DAY_BIT.
static int ac_day_of_week(int year, int month, int day) {
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  if (month < 3) { year -= 1; }
  return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}

static void ac_advance_calendar_day(int *year, int *month, int *day) {
  int dim = ac_days_in_month(*year, *month);
  if (*day < dim) { (*day)++; return; }
  *day = 1;
  if (*month < 12) { (*month)++; return; }
  *month = 1;
  (*year)++;
}

// 1-indexed day-of-year (Jan 1 = 1), via the same leap-aware ac_days_in_month
// table already used for calendar walking.
static int ac_day_of_year(int year, int month, int day) {
  int doy = day;
  for (int m = 1; m < month; m++) { doy += ac_days_in_month(year, m); }
  return doy;
}

// ISO 8601 weekday: 1=Monday..7=Sunday, unlike ac_day_of_week's 0=Sunday..6=Saturday.
static int ac_iso_weekday(int wday) {
  return (wday == 0) ? 7 : wday;
}

// True if `year` has 53 ISO weeks (most years have 52) -- iff Jan 1 is a
// Thursday, or `year` is a leap year and Jan 1 is a Wednesday. Needed by
// ac_iso_week_number to resolve the "is a would-be week 53 actually week 1
// of next year" boundary case.
static bool ac_iso_year_has_53_weeks(int year) {
  int jan1_wday = ac_day_of_week(year, 1, 1);
  if (jan1_wday == 4) { return true; }   // Thursday
  if (ac_is_leap_year(year) && jan1_wday == 3) { return true; }   // Wednesday, leap
  return false;
}

// ISO 8601 week-of-year (1-53). Week boundaries don't align with calendar
// year boundaries: the first few days of January can belong to the last
// week of the PREVIOUS year, and the last few days of December can belong
// to week 1 of the FOLLOWING year -- both handled below via the standard
// ISO formula plus the two edge-case corrections it implies. Still pure
// integer math, no <time.h>, consistent with every other helper here.
static int ac_iso_week_number(int year, int month, int day) {
  int doy = ac_day_of_year(year, month, day);
  int isodow = ac_iso_weekday(ac_day_of_week(year, month, day));
  int week = (doy - isodow + 10) / 7;
  if (week < 1) {
    // Belongs to the previous ISO week-year's last week (52 or 53).
    return ac_iso_year_has_53_weeks(year - 1) ? 53 : 52;
  }
  if (week > 52 && !ac_iso_year_has_53_weeks(year)) {
    // A would-be week 53 that this year doesn't actually have -> it's
    // really week 1 of the following year.
    return 1;
  }
  return week;
}

// Upper bound on how many calendar days ac_cron_first_match_after ever
// walks -- deliberately NOT "large enough to always find a match": every
// (dom_mask, month_mask, dow_mask) combination is allowed, including ones
// that only ever match a handful of times a decade (e.g. day 29 + February)
// or never at all (e.g. day 31 + April only). Since wakeup_schedule() can't
// arm a wakeup more than roughly a year out anyway, there is no point
// searching further than that: this bound IS that practical horizon, with
// a little slack over the ~366-day theoretical max for an ordinary (month
// or day-of-week restricted) pattern. A caller getting -1 back should treat
// it as "no occurrence within about a year", not "malformed input" -- see
// main.c's cron editor, which renders this as "next: never/1y" on the Apply
// row rather than refusing to let the pattern be saved.
#define CRON_DAY_SCAN_MAX 400

// Shared scan behind ac_cron_next_offset_days: first match STRICTLY after
// (from_year, from_month, from_day, from_hour, from_min). Factored out so
// the public function can call it twice (once for the first match, once
// more from just after it) to implement skip_next, without duplicating the
// nested day/hour/minute scan.
static int ac_cron_first_match_after(uint64_t min_mask, uint32_t hour_mask, uint32_t dom_mask,
                                      uint16_t month_mask, uint8_t dow_mask, uint64_t week_mask,
                                      int from_year, int from_month, int from_day,
                                      int from_hour, int from_min,
                                      int *out_hour, int *out_minute) {
  if (min_mask == 0 || hour_mask == 0 || dom_mask == 0 || month_mask == 0 || dow_mask == 0 || week_mask == 0) { return -1; }
  int year = from_year, month = from_month, day = from_day;
  for (int off = 0; off <= CRON_DAY_SCAN_MAX; off++) {
    if (off > 0) { ac_advance_calendar_day(&year, &month, &day); }
    bool day_ok = (month_mask & AC_MONTH_BIT(month))
               && (dom_mask & AC_DOM_BIT(day))
               && (dow_mask & AC_DAY_BIT(ac_day_of_week(year, month, day)))
               && (week_mask & AC_WEEK_BIT(ac_iso_week_number(year, month, day)));
    if (!day_ok) { continue; }
    int start_hour = (off == 0) ? from_hour : 0;
    for (int hour = start_hour; hour <= 23; hour++) {
      if (!(hour_mask & (1u << hour))) { continue; }
      int start_min = (off == 0 && hour == from_hour) ? from_min + 1 : 0;
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

int ac_cron_next_offset_days(uint64_t min_mask, uint32_t hour_mask, uint32_t dom_mask,
                              uint16_t month_mask, uint8_t dow_mask, uint64_t week_mask, bool skip_next,
                              int now_year, int now_month, int now_day, int now_hour, int now_min,
                              int *out_hour, int *out_minute) {
  int hour1, min1;
  int off1 = ac_cron_first_match_after(min_mask, hour_mask, dom_mask, month_mask, dow_mask, week_mask,
                                        now_year, now_month, now_day, now_hour, now_min, &hour1, &min1);
  if (off1 < 0 || !skip_next) {
    if (off1 >= 0) {
      if (out_hour) { *out_hour = hour1; }
      if (out_minute) { *out_minute = min1; }
    }
    return off1;
  }
  // skip_next: the match at off1/hour1/min1 is skipped; find the next one
  // strictly after IT. Advance (year, month, day) by off1 days from `now`
  // to get the skipped match's own calendar date to resume from.
  int year2 = now_year, month2 = now_month, day2 = now_day;
  for (int i = 0; i < off1; i++) { ac_advance_calendar_day(&year2, &month2, &day2); }
  int off2 = ac_cron_first_match_after(min_mask, hour_mask, dom_mask, month_mask, dow_mask, week_mask,
                                        year2, month2, day2, hour1, min1, out_hour, out_minute);
  if (off2 < 0) {
    // Unreachable given validated non-zero masks (same guarantee as
    // ac_next_offset_days's own "unreachable" fallback) -- defensively fall
    // back to the skipped match itself rather than reporting "never".
    if (out_hour) { *out_hour = hour1; }
    if (out_minute) { *out_minute = min1; }
    return off1;
  }
  return off1 + off2;
}

bool ac_cron_is_due(uint64_t min_mask, uint32_t hour_mask, uint32_t dom_mask, uint16_t month_mask,
                     uint8_t dow_mask, uint64_t week_mask, bool enabled,
                     int now_year, int now_month, int now_day, int now_wday, int now_hour, int now_min,
                     int32_t now_epoch_min, int32_t last_fired_min) {
  if (!enabled) { return false; }
  if (last_fired_min == now_epoch_min) { return false; }   // already handled this exact minute
  if (!(month_mask & AC_MONTH_BIT(now_month))) { return false; }
  if (!(dom_mask & AC_DOM_BIT(now_day))) { return false; }
  if (!(dow_mask & AC_DAY_BIT(now_wday))) { return false; }
  if (!(week_mask & AC_WEEK_BIT(ac_iso_week_number(now_year, now_month, now_day)))) { return false; }
  if (!(hour_mask & (1u << now_hour))) { return false; }
  if (!(min_mask & (1ULL << now_min))) { return false; }
  return true;
}

int32_t ac_cron_last_match_in_range(uint64_t min_mask, uint32_t hour_mask, uint32_t dom_mask, uint16_t month_mask,
                                    uint8_t dow_mask, uint64_t week_mask, bool enabled,
                                    int start_year, int start_month, int start_day, int start_hour, int start_min,
                                    int32_t start_epoch_min, int count, int32_t last_fired_min) {
  if (!enabled) { return -1; }
  int year = start_year, month = start_month, day = start_day, hour = start_hour, minute = start_min;
  bool day_ok = false, day_known = false;
  int32_t best = -1;
  for (int i = 0; i < count; i++) {
    if (i > 0) {
      if (++minute == 60) {
        minute = 0;
        if (++hour == 24) { hour = 0; ac_advance_calendar_day(&year, &month, &day); day_known = false; }
      }
    }
    if (!day_known) {
      day_ok = (month_mask & AC_MONTH_BIT(month))
            && (dom_mask & AC_DOM_BIT(day))
            && (dow_mask & AC_DAY_BIT(ac_day_of_week(year, month, day)))
            && (week_mask & AC_WEEK_BIT(ac_iso_week_number(year, month, day)));
      day_known = true;
    }
    int32_t epoch_min = start_epoch_min + i;
    if (day_ok && (hour_mask & (1u << hour)) && (min_mask & (1ULL << minute)) && epoch_min > last_fired_min) {
      best = epoch_min;
    }
  }
  return best;
}

void ac_format_cron_summary(char *buf, size_t n, const char *min_str, const char *hour_str,
                             const char *dom_str, const char *month_str, const char *dow_str,
                             const char *week_str) {
  snprintf(buf, n, "%s %s %s %s %s %s", min_str ? min_str : "*", hour_str ? hour_str : "*",
           dom_str ? dom_str : "*", month_str ? month_str : "*", dow_str ? dow_str : "*",
           week_str ? week_str : "*");
}
