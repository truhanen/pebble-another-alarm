// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_ALARMS 16
#define NAME_LEN   31
#define CRON_FIELD_LEN 28   // 27 usable chars + NUL; fits comma-lists like "0-59/5,10,20-25/3"

// Custom sound/vibe pattern: a space-separated sequence of on-beats and
// (optional) breaks, each a duration digit chosen from AC_NOTE_DURATION_DIGITS
// -- e.g. "1C 2D 1- 1C 2D" (sound) or "2 1- 2 1- 2" (vibe). Breaks are
// optional between on-beats (two on-beats with no break between them play
// back to back with no gap); a break is written as the duration digit
// followed by '-'. Sound (Tone) and vibe (Vibration) have DIFFERENT caps,
// on request: vibe halved to 8 (short buzz patterns rarely need more, and
// it keeps VibePattern's own run-length-encoded array small), sound/tone
// doubled to 32 (tones -- including the custom-tone library, sound-only --
// benefit from more headroom for longer musical phrases like Imperial
// March). Each was 16 (a single shared constant) before this split.
#define AC_VIBE_PATTERN_MAX_SEGMENTS 8
#define AC_SOUND_PATTERN_MAX_SEGMENTS 32

// Each stored pattern segment is one byte: bit 7 set means a break (silence/
// no-vibrate); bit 7 clear means an on-beat (for sound, its pitch lives in
// the parallel sound_pattern_note[] array; for vibe there's no pitch at
// all). Bits 0-6 hold the duration digit itself -- one of
// AC_NOTE_DURATION_DIGITS, e.g. a stored 4 means "4 x 100ms == 400ms" --
// not an arbitrary index, so the digit IS the duration in hundreds of ms;
// no separate ms lookup table is needed, just digit*100.
#define AC_PATTERN_BREAK_BIT 0x80u
#define AC_PATTERN_DURATION_MASK 0x7Fu

// Every 100ms increment from 100-800ms is a valid duration digit -- one per
// pattern-keyboard character key (0-7), replacing what used to be an
// arbitrary raw millisecond value per segment -- so every alarm's "1" means
// the same 100ms. Each digit IS its duration in hundreds of ms (1=100ms,
// 2=200ms, ..., 8=800ms), rather than an arbitrary index into a lookup
// table, so the digits themselves are informative to read even outside the
// keyboard/app. Defined in alarm_calc.c; AC_NOTE_DURATION_COUNT is
// AC_NOTE_DURATION_DIGITS' length. ac_vibe_pattern_parse/
// ac_sound_pattern_parse reject any digit not in this set (i.e. anything
// outside 1-8) -- "0" and "9" are plain ASCII digits but invalid duration
// digits. (An earlier version of this set was sparse -- {1,2,4,8}, each
// double the last like real rhythm notation -- widened to the full
// contiguous 1-8 range on request, one digit per keyboard key; see
// multitap_keyboard.c's PATTERN_KEY_SOUND/PATTERN_KEY_VIBE.)
#define AC_NOTE_DURATION_COUNT 8
extern const uint8_t AC_NOTE_DURATION_DIGITS[AC_NOTE_DURATION_COUNT];   // { 1, 2, 3, 4, 5, 6, 7, 8 }
#define AC_NOTE_DURATION_MS_PER_UNIT 100

// Note letters for a custom SOUND pattern's on-beats (vibe has no pitch, so
// no equivalent table there) -- a diatonic scale, C-B (one per pattern
// keyboard key 0-6) -- see AC_NOTE_MIDI below for the actual base-octave
// pitch each maps to. AC_NOTE_COUNT is its length.
#define AC_NOTE_COUNT 7
extern const char AC_NOTE_LETTER[AC_NOTE_COUNT];   // 'C','D','E','F','G','A','B'
extern const uint8_t AC_NOTE_MIDI[AC_NOTE_COUNT];  // base-octave MIDI note number per AC_NOTE_LETTER entry

// A note's octave relative to its own AC_NOTE_MIDI base pitch -- real ABC
// notation's convention for shifting a note's octave: one or more
// apostrophes "'" written right after the letter shift it up an octave
// each, one or more commas "," shift it down an octave each (e.g. "C" =
// base, "C'" = up 1, "C,,," = down 3); no marker at all means
// AC_NOTE_OCTAVE_SAME. Applied as
// `AC_NOTE_MIDI[base] + 12 * (octave - AC_NOTE_OCTAVE_SAME)`.
//
// The up/down range is deliberately asymmetric, derived from keeping every
// one of the 7 notes' resulting MIDI pitch within the valid 0-127 range
// (nothing here clamps a computed pitch, so this cap is the only thing
// keeping it in range): the highest base note, B (AC_NOTE_MIDI 107), would
// exceed 127 at 2 octaves up (131), so AC_NOTE_OCTAVE_MAX_UP is 1 for every
// note, uniformly (no per-note special-casing -- simpler, and matches this
// grammar's existing all-or-nothing validation style elsewhere). The
// lowest base note, C (96), would go negative at 9 octaves down (96-108),
// so AC_NOTE_OCTAVE_MAX_DOWN is 8. AC_NOTE_OCTAVE_SAME is the "zero point"
// octave field value the shift is measured from -- chosen as
// AC_NOTE_OCTAVE_MAX_DOWN so the full down range (0..AC_NOTE_OCTAVE_MAX_DOWN)
// and the up range (AC_NOTE_OCTAVE_SAME+1..AC_NOTE_OCTAVE_SAME+
// AC_NOTE_OCTAVE_MAX_UP) both stay non-negative field values.
#define AC_NOTE_OCTAVE_MAX_DOWN 8
#define AC_NOTE_OCTAVE_MAX_UP   1
#define AC_NOTE_OCTAVE_SAME (AC_NOTE_OCTAVE_MAX_DOWN)

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

// Day-of-month (1-31) and month (1-12) bitmasks for cron mode's dom/month
// fields -- 1-indexed (bit 0 unused) since "day 0"/"month 0" don't exist,
// unlike AC_DAY_BIT's 0-indexed weekday convention.
#define AC_DOM_BIT(d) (1u << (d))
#define AC_DOM_ALL 0xFFFFFFFEu     // bits 1-31 set
#define AC_MONTH_BIT(m) (1u << (m))
#define AC_MONTH_ALL 0x1FFEu       // bits 1-12 set

// ISO 8601 week-of-year (1-53) bitmask for cron mode's week field --
// 1-indexed like AC_DOM_BIT/AC_MONTH_BIT. Needs a 64-bit mask: bit 53
// doesn't fit in a uint32_t the way AC_DOM_BIT/AC_MONTH_BIT's smaller
// ranges do.
#define AC_WEEK_BIT(w) (1ULL << (w))
#define AC_WEEK_ALL 0x3FFFFFFFFFFFFEULL   // bits 1-53 set

typedef struct {
  uint32_t id;             // stable identity, 0 = none assigned yet
  char name[NAME_LEN + 1];
  uint8_t hour, minute;     // local time of day
  bool repeats;             // true = fires weekly forever on repeat_days; false = fires once
  uint8_t repeat_days;      // AC_DAY_* bitmask. When !repeats: 0 = next occurrence of the
                            // time (today/tomorrow), or a single bit = fire once on that
                            // specific weekday. When repeats: the weekly recurrence days.
  bool enabled;
  bool skip_next;           // skip just the next occurrence (repeats only for
                            // legacy alarms; also reused for is_cron alarms
                            // -- see the is_cron block below)
  uint16_t snooze_minutes;
  uint8_t snooze_max;       // 0 = unlimited
  bool vibration_enabled;
  bool sound_enabled;
  bool auto_dismiss;            // ring screen fires vibration/sound (per the two
                             // toggles above) exactly once instead of the
                             // usual repeating buzz, then auto-dismisses
                             // itself after a couple seconds -- same end
                             // state as the user pressing Stop. See main.c's
                             // trigger_alarm()/alarm_auto_dismiss_cb().
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
  // above: minute/hour/day-of-month/month/ISO-week/day-of-week each given as
  // an independent cron-style field ("*", "12", "1-2", "*/20", "4-45/10",
  // comma-lists of these), matched every minute rather than fired at most
  // once per day. When is_cron, `repeats` is unused (forced false/ignored)
  // and `repeat_days` is REPURPOSED to hold the parsed day-of-week mask
  // (same AC_DAY_* bits) instead of the legacy weekly-repeat days — no
  // separate field needed since it's exactly the same bitmask shape either
  // way. `skip_next` is ALSO reused (not repurposed -- same "skip just the
  // next occurrence" meaning as legacy): ac_cron_next_offset_days() skips
  // the very next matching minute and reports the one after when it's set,
  // exactly mirroring how ac_next_offset_days already does this for legacy
  // alarms. It's cleared by the caller (main.c) once the resumed
  // (non-skipped) occurrence actually fires.
  //
  // Every combination of the six fields is allowed, including day-of-month
  // AND day-of-week both restricted at once, and day 29 combined with
  // February -- unlike real cron, this engine always ANDs cron_dom_mask and
  // repeat_days together rather than OR-ing them when both are restricted,
  // which is what makes patterns like "day 1-7 AND weekday Monday" (first
  // Monday of every month) expressible at all, with no special-case
  // matching logic. cron_week_mask (ISO 8601 week-of-year, 1-53) is ANDed in
  // the same unconditional way, which is what makes biweekly patterns
  // ("*/2" in week) expressible. There's no validation rejecting
  // rare-or-impossible combinations (e.g. day 29 + February, day 31 +
  // April, or week 53 in a year that doesn't have one) either --
  // ac_cron_next_offset_days() simply may not find a match within its
  // bounded forward search (CRON_DAY_SCAN_MAX, alarm_calc.c, tied to
  // wakeup_schedule()'s own ~1-year scheduling horizon), which the cron
  // editor surfaces as a live "next: never/1y" preview on its Apply row
  // instead of refusing to let the pattern be saved.
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
  // cron_dom_mask/cron_month_mask (and the raw text below) are appended
  // here, after vibe_pattern/increasing_volume, per the same append-only
  // rule -- a freshly zero-filled mask on schema migration would mean
  // "matches nothing" (unlike increasing_volume's 0=off, which was already
  // the correct default), so store_load() must explicitly re-fill these to
  // AC_DOM_ALL/AC_MONTH_ALL for any migrated is_cron alarm -- see
  // alarm_store.c.
  uint32_t cron_dom_mask;    // bit i (1-31, AC_DOM_BIT) set => day-of-month i matches
  uint16_t cron_month_mask;  // bit i (1-12, AC_MONTH_BIT) set => month i matches
  char cron_dom[CRON_FIELD_LEN];    // raw text, e.g. "1,15" — parses into cron_dom_mask
  char cron_month[CRON_FIELD_LEN];  // raw text, e.g. "3-9" — parses into cron_month_mask
  // cron_week_mask (and its raw text) are appended here for the same reason
  // -- see AC_WEEK_ALL's migration-fixup note in alarm_store.c. ISO 8601
  // week-of-year (1-53), ANDed in alongside every other cron field with no
  // special case, same as dom/month before it.
  uint64_t cron_week_mask;   // bit i (1-53, AC_WEEK_BIT) set => ISO week i matches
  char cron_week[CRON_FIELD_LEN];   // raw text, e.g. "*/2" — parses into cron_week_mask

  // Custom sound/vibe patterns (see AC_SOUND_PATTERN_MAX_SEGMENTS/
  // AC_VIBE_PATTERN_MAX_SEGMENTS/AC_PATTERN_BREAK_BIT above and
  // ac_sound_pattern_parse/ac_vibe_pattern_parse below), appended
  // here per the same append-only rule. *_kind==0 ("Default"/"Preset") is
  // already the correct migration default -- unlike cron_dom_mask/
  // cron_week_mask, a zero-filled kind doesn't mean "matches nothing", it
  // means "keep behaving exactly as before this feature existed" (sound:
  // the fixed beep loop; vibe: whatever vibe_pattern already held) -- so no
  // store_load() fixup is needed for the *_kind fields themselves. The
  // *_len/pattern arrays DO need an explicit fixup on migration from before
  // STORE_SCHEMA 13 though -- see alarm_store.c: the byte encoding changed
  // (was a raw 1-100 duration; now bit 7 = break, bits 0-6 = duration
  // digit), so an old *_kind==1 alarm's stored bytes would otherwise be
  // silently reinterpreted as garbage under the new scheme.
  uint8_t sound_pattern_kind;   // 0 = Default (existing fixed beep loop), 1 = Custom
  uint8_t sound_pattern_len;    // number of segments in sound_pattern[]/sound_pattern_note[]; 0 = never set
  uint8_t sound_pattern[AC_SOUND_PATTERN_MAX_SEGMENTS];        // bit7=is_break, bits0-6=duration digit (one of AC_NOTE_DURATION_DIGITS)
  uint8_t vibe_pattern_kind;    // 0 = Preset (use vibe_pattern above), 1 = Custom
  uint8_t vibe_pattern_len;     // number of segments in vibe_pattern_custom[]; 0 = never set
  uint8_t vibe_pattern_custom[AC_VIBE_PATTERN_MAX_SEGMENTS];  // bit7=is_break, bits0-6=duration digit (one of AC_NOTE_DURATION_DIGITS)
  // sound_pattern_note is a brand-new array (didn't exist before STORE_SCHEMA
  // 13), appended here at the very end rather than next to sound_pattern
  // above, per the append-only rule -- see alarm_store.c's migration fixup
  // for why sound_pattern/vibe_pattern_custom's REINTERPRETED byte meaning
  // (not just this new array) also needs an explicit reset on migration.
  uint8_t sound_pattern_note[AC_SOUND_PATTERN_MAX_SEGMENTS];   // octave*AC_NOTE_COUNT + AC_NOTE_LETTER/AC_NOTE_MIDI index; meaningless when is_break

  // Appended for the custom-tone library (see CustomTone below) -- valid
  // iff sound_pattern_kind == 2 ("Library"), a third value alongside the
  // existing 0 (Default)/1 (inline Custom, unchanged). 0 = no reference.
  // Zero-fills correctly on migration, same "already the safe default"
  // category as vibe_pattern/increasing_volume's own past appends.
  uint32_t custom_tone_id;
} Alarm;

// A named, separately-persisted sound pattern any alarm can reference by
// id (Alarm.custom_tone_id, when Alarm.sound_pattern_kind == 2) instead of
// storing its own copy -- edits/deletes are a live reference, so every
// alarm pointing at the same id sees them. Mirrors the exact
// (kind-less)/len/pattern/pattern_note shape Alarm already uses for its
// own inline custom sound, so ac_sound_pattern_parse/ac_sound_pattern_
// format work on it unchanged. Persisted independently of Alarm/
// STORE_SCHEMA -- see TONE_STORE_SCHEMA, alarm_store.h.
#define MAX_CUSTOM_TONES 20

typedef struct {
  uint32_t id;              // stable, allocated once (store_alloc_tone_id), never reused; 0 = unused slot
  char name[NAME_LEN + 1];  // always non-empty for a saved tone (enforced by main.c's UI, not here)
  uint8_t pattern_len;
  uint8_t pattern[AC_SOUND_PATTERN_MAX_SEGMENTS];       // bit7=is_break, bits0-6=duration digit
  uint8_t pattern_note[AC_SOUND_PATTERN_MAX_SEGMENTS];  // octave*AC_NOTE_COUNT + AC_NOTE_LETTER/AC_NOTE_MIDI index
} CustomTone;

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

// Parses a custom VIBE pattern string: whitespace-separated tokens, each
// either a bare duration digit ("2", meaning vibrate ON for digit*
// AC_NOTE_DURATION_MS_PER_UNIT ms) or a duration digit followed by '-'
// ("1-", meaning a break/pause of that duration) -- e.g. "2 1- 2 1- 2".
// Breaks are optional: two on-tokens with nothing between them play back
// to back with no gap. `digit` must be one of AC_NOTE_DURATION_DIGITS
// (1, 2, 4, or 8 -- not any digit 1-9); token count must be
// 1..max_segments. On success, writes one packed byte per segment (bit 7 =
// AC_PATTERN_BREAK_BIT if a break, bits 0-6 = the duration digit) into out
// (capacity max_segments) and the count into *out_len, returning true.
// Returns false (leaving out/*out_len untouched) on ANY malformed token
// (non-digit, digit out of range, trailing garbage after '-'), an empty/
// whitespace-only string, or more than max_segments tokens.
bool ac_vibe_pattern_parse(const char *text, uint8_t max_segments, uint8_t *out, uint8_t *out_len);

// Formats a parsed vibe pattern back into the canonical space-joined text
// ac_vibe_pattern_parse would itself accept and round-trip exactly. Writes
// into buf (size n); empty string (not "0 segments") if len == 0.
void ac_vibe_pattern_format(char *buf, size_t n, const uint8_t *pattern, uint8_t len);

// Parses a custom SOUND pattern string: like ac_vibe_pattern_parse, but
// every on-token MUST carry a pitch -- a duration digit followed by exactly
// one note letter from AC_NOTE_LETTER (e.g. "1C"), never a bare digit alone
// (unlike vibe, where a bare digit is a valid on-token) -- a break is still
// just the duration digit followed by '-' ("1-"), with no letter, e.g.
// "1C 2D 1- 1C 2D". An on-token's note letter may optionally be followed by
// one or more octave markers of the SAME kind -- "'" (apostrophe) shifts up
// an octave each, "," (comma) shifts down an octave each, real ABC-notation
// convention (e.g. "1C'" up 1, "1C,,," down 3); no marker means
// AC_NOTE_OCTAVE_SAME. Up is capped at AC_NOTE_OCTAVE_MAX_UP (1) and down at
// AC_NOTE_OCTAVE_MAX_DOWN (8) -- see that constant's own doc comment for why
// the range is asymmetric; exceeding either cap, or mixing "'" and ","
// within one token, is malformed. On success, writes one packed
// duration/break byte per segment into out_flags (same encoding as
// ac_vibe_pattern_parse) and `octave*AC_NOTE_COUNT + <AC_NOTE_LETTER/
// AC_NOTE_MIDI index>` into out_notes (capacity max_segments each; a break
// segment's out_notes entry is written as 0 but is meaningless -- callers
// must check the break bit first), and the count into *out_len, returning
// true. Returns false (leaving every output untouched) on any malformed
// token (missing/invalid letter on an on-token, a bare digit with no
// suffix, digit out of range, an unrecognized letter, an octave marker
// with no preceding letter, mixed markers, or a marker run past its cap),
// an empty/whitespace-only string, or more than max_segments tokens.
bool ac_sound_pattern_parse(const char *text, uint8_t max_segments,
                             uint8_t *out_flags, uint8_t *out_notes, uint8_t *out_len);

// Formats a parsed sound pattern back into the canonical space-joined text
// ac_sound_pattern_parse would itself accept and round-trip exactly. Writes
// into buf (size n); empty string (not "0 segments") if len == 0.
void ac_sound_pattern_format(char *buf, size_t n, const uint8_t *flags, const uint8_t *notes, uint8_t len);

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

// Day offset (0..CRON_DAY_SCAN_MAX, see alarm_calc.c) + hour/minute of this
// cron pattern's next match STRICTLY after `now` (mirroring
// ac_next_offset_days always reporting the next FUTURE occurrence).
// dom_mask uses AC_DOM_BIT, month_mask uses AC_MONTH_BIT, week_mask uses
// AC_WEEK_BIT (ISO 8601 week-of-year), dow_mask uses AC_DAY_BIT like
// repeat_days. now_year/now_month/now_day are needed (unlike the 3-field
// original) because dom_mask/month_mask/week_mask restrict which real
// calendar dates match, which requires walking real calendar days (month
// lengths, leap years, ISO week-year boundaries) rather than a bare 7-day
// weekday wraparound -- alarm_calc.c still has no <time.h> dependency; see
// ac_is_leap_year/ac_days_in_month/ac_day_of_week/ac_iso_week_number in
// alarm_calc.c, all pure integer math. dom_mask/month_mask/week_mask are
// ANDed in alongside dow_mask/hour_mask/min_mask with NO special case for
// multiple fields being restricted at once -- unlike real cron, which ORs
// day-of-month and day-of-week together when both are restricted, this
// engine always ANDs them, which is what makes patterns like "day 1-7 AND
// weekday Monday" (first Monday of every month) or "week */2" (biweekly)
// expressible. Every combination is allowed, including ones that rarely or
// never produce a real calendar date (day 29 + February, day 31 + April,
// week 53 in a year without one) -- this function just may not find a
// match within CRON_DAY_SCAN_MAX days, which is an ordinary, expected
// result (see that constant's own comment), not a sign of invalid input.
// Returns -1 (out_hour/out_minute left unset) if no bit is set anywhere in
// any of the six masks, or no match is found within the scan bound.
// `skip_next`: same meaning as ac_next_offset_days's own skip_next handling
// -- when true, the first match is skipped and the SECOND match strictly
// after `now` is reported instead.
int ac_cron_next_offset_days(uint64_t min_mask, uint32_t hour_mask, uint32_t dom_mask,
                              uint16_t month_mask, uint8_t dow_mask, uint64_t week_mask, bool skip_next,
                              int now_year, int now_month, int now_day, int now_hour, int now_min,
                              int *out_hour, int *out_minute);

// True if the cron pattern matches the EXACT current (now_year, now_month,
// now_day, now_wday, now_hour, now_min) and hasn't already been marked
// fired for this exact minute. now_epoch_min (caller-computed, e.g.
// now_s()/60 in main.c) is the minute-granularity sibling of ac_is_due's
// today_day_id — alarm_calc.c stays <time.h>-free, main.c owns turning
// wall-clock time into it. now_year IS needed here (unlike the 5-field
// version of this function) because ac_iso_week_number() needs it to
// resolve ISO week-year boundary cases (a late-December date can belong to
// week 1 of the FOLLOWING year). Unlike ac_is_due, there is no "already
// fired today" guard: a cron alarm can fire many times a day, so only
// exact-minute dedup (last_fired_min == now_epoch_min) blocks a
// re-trigger, never a whole day.
bool ac_cron_is_due(uint64_t min_mask, uint32_t hour_mask, uint32_t dom_mask, uint16_t month_mask,
                     uint8_t dow_mask, uint64_t week_mask, bool enabled,
                     int now_year, int now_month, int now_day, int now_wday, int now_hour, int now_min,
                     int32_t now_epoch_min, int32_t last_fired_min);

// Space-joined raw cron field strings in cron field order, e.g.
// "0-59/20 * 1 3-9 1-5 27" (min hour dom month dow week) — truncated by the
// caller's own draw call (GTextOverflowModeTrailingEllipsis), same as every
// other ac_format_* helper. Writes into buf (size n).
void ac_format_cron_summary(char *buf, size_t n, const char *min_str, const char *hour_str,
                             const char *dom_str, const char *month_str, const char *dow_str,
                             const char *week_str);
