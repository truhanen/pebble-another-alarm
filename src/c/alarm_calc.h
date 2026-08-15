// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_ALARMS 16
#define NAME_LEN   31
#define CRON_FIELD_LEN 28   // 27 usable chars + NUL; fits comma-lists like "0-59/5,10,20-25/3"

// Custom sound/vibe pattern: an ABC-notation subset. A space-separated
// sequence of on-beats (NOTE tokens for sound, bare-duration tokens for
// vibe -- vibe has no pitch, so it has no letter to attach anything to) and
// (optional) rests, each carrying a duration expressed as a ratio relative
// to AC_NOTE_DEFAULT_MS -- e.g. "^C/2 z C3 _D" (sound) or "1 z/2 1" (vibe).
// A rest is written as 'z' (real ABC's own rest token) followed by an
// optional duration suffix; two on-tokens with nothing between them play
// back to back with no gap, same as before. Sound (Tune) and vibe
// (Vibration) have DIFFERENT caps, on request: vibe halved to 8 (short buzz
// patterns rarely need more, and it keeps VibePattern's own run-length-
// encoded array small), sound/tune doubled to 32 (tunes -- including the
// custom-tune library, sound-only -- benefit from more headroom for longer
// musical phrases like Imperial March). Each was 16 (a single shared
// constant) before this split.
#define AC_VIBE_PATTERN_MAX_SEGMENTS 8
#define AC_SOUND_PATTERN_MAX_SEGMENTS 32

// Each stored pattern segment is one byte: bit 7 set means a rest (silence/
// no-vibrate, a 'z' token); bit 7 clear means an on-beat (for sound, its
// pitch lives in the parallel sound_pattern_note[] array; for vibe there's
// no pitch at all). Bits 0-6 hold a duration RATIO packed as
// `(N-1)*8 + (D-1)`, N,D each one of AC_NOTE_RATIO_DIGITS (1-8), reduced to
// lowest terms by their GCD before packing so two spellings of the same
// ratio (e.g. "1/1" and "2/2") always produce the identical byte -- this
// matters because vibe_preset_match() (main.c) identifies a preset by
// memcmp-ing raw bytes, which would silently break if the same duration
// could round-trip to different byte values depending on how it was typed.
// Actual duration is `round(AC_NOTE_DEFAULT_MS * N / D)` ms -- N/D=1/1 (no
// duration suffix at all, "bare") means exactly AC_NOTE_DEFAULT_MS. Payload
// max is 7*8+7=63, comfortably inside the 6 available bits (bit 6 unused/
// reserved).
#define AC_PATTERN_REST_BIT 0x80u
#define AC_PATTERN_DURATION_MASK 0x7Fu

// The 8 valid digits for either side of a duration ratio ("N" or "D" in
// "[N]['/'D]") -- one per pattern-keyboard character key, same set used
// twice (once per ratio side) rather than once for a whole absolute
// duration the way it used to be. AC_NOTE_RATIO_DIGIT_COUNT is
// AC_NOTE_RATIO_DIGITS' length. ac_vibe_pattern_parse/ac_sound_pattern_parse
// reject any digit not in this set for either N or D (i.e. anything outside
// 1-8) -- "0" and "9" are plain ASCII digits but invalid ratio digits, same
// exclusion as before, deliberately kept (rather than widening the range or
// allowing multi-digit N/D) so the keyboard needs no new character and no
// digit exceeding what a single keypress already types.
#define AC_NOTE_RATIO_DIGIT_COUNT 8
extern const uint8_t AC_NOTE_RATIO_DIGITS[AC_NOTE_RATIO_DIGIT_COUNT];   // { 1, 2, 3, 4, 5, 6, 7, 8 }

// The "bare" duration (no explicit "[N]['/'D]" suffix at all) means exactly
// this many ms -- 100, matching the old grammar's own per-digit unit
// (AC_NOTE_DURATION_MS_PER_UNIT used to be 100 too), so every old absolute
// duration digit N (which meant N*100ms) translates losslessly to the new
// ratio digit N with NO fraction ever needed (old digit 1 becomes bare
// itself, digits 2-8 become plain "N") -- a 200ms base was considered
// (200ms, the old digit "2", was the single most-used value across every
// existing preset) since it shortens the single most common note to zero
// characters, but was rejected in favor of 100ms: half the old digits
// (1,3,5,7) would then need an explicit fraction (e.g. "/2", "3/2") to
// reproduce their old timing, and the fraction syntax existing mainly to
// re-express old, already-common values (rather than as a genuinely new,
// occasionally-reached-for capability) was judged worse than the smaller
// keystroke saving on the single most common note. Only sound tokens can
// actually spell "bare", since sound's note letter anchors an empty
// duration suffix; vibe's on-beat token has no letter to anchor to, so an
// empty vibe token is ambiguous/unparseable -- vibe must always spell an
// explicit ratio (e.g. "1" for this same 100ms), same as it always had to
// spell an explicit digit under the old grammar. No functional loss for
// vibe, just no new bare-token convenience there.
#define AC_NOTE_DEFAULT_MS 100

// A sound on-beat's accidental, packed into its pattern_note[] byte
// alongside octave/letter (see the encoding note above AC_NOTE_LETTER
// below) -- natural is the default (no prefix character at all); sharp
// ("^", real ABC notation) and flat ("_", real ABC notation) are both
// supported even though every flat pitch is enharmonically reachable as
// some other note's sharp, a deliberate readability choice (spelling a
// pattern the way it's actually thought of) rather than a reachability gap.
#define AC_NOTE_ACCIDENTAL_NATURAL 0
#define AC_NOTE_ACCIDENTAL_SHARP   1
#define AC_NOTE_ACCIDENTAL_FLAT    2
#define AC_NOTE_ACCIDENTAL_COUNT   3

// Note letters for a custom SOUND pattern's on-beats (vibe has no pitch, so
// no equivalent table there) -- a diatonic scale, C-B (one per pattern
// keyboard key) -- see AC_NOTE_MIDI below for the actual base-octave pitch
// each maps to. AC_NOTE_COUNT is its length.
//
// A sound on-beat's pattern_note[] byte packs THREE fields arithmetically
// (not bit-packed): `(octave*AC_NOTE_COUNT + note_index)*AC_NOTE_ACCIDENTAL_COUNT
// + accidental`. octave ranges 0..AC_NOTE_OCTAVE_SAME+AC_NOTE_OCTAVE_MAX_UP
// (0..9), note_index 0..AC_NOTE_COUNT-1 (0..6), so the combined
// octave*7+note_index term maxes at 9*7+6=69; multiplying by
// AC_NOTE_ACCIDENTAL_COUNT (3) and adding accidental (0-2) maxes the whole
// value at 69*3+2=209 -- comfortably inside a uint8_t (0-255), with no
// array growth needed over the old 2-field encoding. Decode:
// `accidental = value % 3; combined = value / 3; octave = combined / 7;
// note_index = combined % 7`. Meaningless on a rest segment, same as before.
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

  // Custom VIBE patterns only now (see AC_VIBE_PATTERN_MAX_SEGMENTS/
  // AC_PATTERN_BREAK_BIT above and ac_vibe_pattern_parse below), appended
  // here per the same append-only rule. `vibe_pattern_kind==0` ("Preset")
  // is already the correct migration default -- a zero-filled kind means
  // "keep behaving exactly as before this feature existed" (whatever
  // vibe_pattern already held), so no store_load() fixup is needed for the
  // kind field itself. `vibe_pattern_len`/`vibe_pattern_custom[]` DO need
  // an explicit fixup on migration from before STORE_SCHEMA 13 (and again
  // 14) though -- see alarm_store.c: the byte encoding has changed since
  // then (was a raw 1-100 duration; now bit 7 = break, bits 0-6 = duration
  // digit), so an old kind==1 alarm's stored bytes would otherwise be
  // silently reinterpreted as garbage under the new scheme.
  //
  // `sound_pattern_kind` used to have an analogous inline-Custom
  // representation (kind==1, backed by `sound_pattern[]`/
  // `sound_pattern_note[]`) -- removed (not just relocated) once tracing
  // every call site confirmed kind==1 could no longer be reached except by
  // copying a SOUND_PRESETS[] entry's own bytes verbatim (the tune library,
  // kind==2, is sound's only remaining path for genuinely custom content).
  // `sound_pattern_kind` now only ever takes 0 ("Preset", use
  // `sound_preset` below) or 2 ("Library", use `custom_tune_id` below).
  uint8_t sound_pattern_kind;   // 0 = Preset (use sound_preset below), 2 = Library (use custom_tune_id below)
  uint8_t vibe_pattern_kind;    // 0 = Preset (use vibe_pattern above), 1 = Custom
  uint8_t vibe_pattern_len;     // number of segments in vibe_pattern_custom[]; 0 = never set
  uint8_t vibe_pattern_custom[AC_VIBE_PATTERN_MAX_SEGMENTS];  // bit7=is_rest, bits0-6=packed N/D duration ratio (AC_PATTERN_DURATION_MASK)

  // Appended for the custom-tune library (see CustomTune below) -- valid
  // iff sound_pattern_kind == 2 ("Library"), a third value alongside the
  // existing 0 (Default/Preset). 0 = no reference. Zero-fills correctly on
  // migration, same "already the safe default" category as vibe_pattern/
  // increasing_volume's own past appends.
  uint32_t custom_tune_id;

  // Sound's own preset index, mirroring vibe_pattern's own index-into-a-
  // preset-table approach (an index into SOUND_PRESETS[], main.c) --
  // appended last, after removing the old inline-Custom representation
  // (sound_pattern_len/sound_pattern[]/sound_pattern_note[], a genuine
  // struct resize/removal, not an append -- see STORE_SCHEMA's own
  // history comment, alarm_store.h, for why this is a deliberate, accepted
  // hard break rather than something store_load() tries to migrate
  // through). 0 zero-fills correctly on migration (same "already the safe
  // default" category as vibe_pattern's own index).
  uint8_t sound_preset;
} Alarm;

// A named, separately-persisted sound pattern any alarm can reference by
// id (Alarm.custom_tune_id, when Alarm.sound_pattern_kind == 2) instead of
// storing its own copy -- edits/deletes are a live reference, so every
// alarm pointing at the same id sees them. Mirrors the exact
// (kind-less)/len/pattern/pattern_note shape Alarm already uses for its
// own inline custom sound, so ac_sound_pattern_parse/ac_sound_pattern_
// format work on it unchanged. Persisted independently of Alarm/
// STORE_SCHEMA -- see TUNE_STORE_SCHEMA, alarm_store.h.
#define MAX_CUSTOM_TUNES 20

typedef struct {
  uint32_t id;              // stable, allocated once (store_alloc_tune_id), never reused; 0 = unused slot
  char name[NAME_LEN + 1];  // always non-empty for a saved tune (enforced by main.c's UI, not here)
  uint8_t pattern_len;
  uint8_t pattern[AC_SOUND_PATTERN_MAX_SEGMENTS];       // bit7=is_rest, bits0-6=packed N/D duration ratio (AC_PATTERN_DURATION_MASK)
  uint8_t pattern_note[AC_SOUND_PATTERN_MAX_SEGMENTS];  // (octave*AC_NOTE_COUNT+note_index)*AC_NOTE_ACCIDENTAL_COUNT+accidental
} CustomTune;

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
// either an on-beat (a duration ratio alone, e.g. "1", "2/3", "/4" -- see
// AC_NOTE_DEFAULT_MS's doc comment for why vibe can't spell a bare/empty
// duration the way sound can) or a rest ('z' followed by the same optional
// duration ratio, e.g. "z", "z/2") -- e.g. "1 z/2 1". Rests are optional
// between on-beats: two on-tokens with nothing between them play back to
// back with no gap. Each side of a ratio (N and/or D in "[N]['/'D]") must be
// one of AC_NOTE_RATIO_DIGITS (1-8); token count must be 1..max_segments.
// On success, writes one packed byte per segment (bit 7 = AC_PATTERN_REST_BIT
// if a rest, bits 0-6 = the N/D ratio reduced to lowest terms and packed per
// AC_PATTERN_DURATION_MASK's doc comment) into out (capacity max_segments)
// and the count into *out_len, returning true. Returns false (leaving
// out/*out_len untouched) on ANY malformed token (a vibe on-token with an
// empty duration, a digit out of range on either side of '/', a '/' with no
// following digit, trailing garbage), an empty/whitespace-only string, or
// more than max_segments tokens.
bool ac_vibe_pattern_parse(const char *text, uint8_t max_segments, uint8_t *out, uint8_t *out_len);

// Formats a parsed vibe pattern back into the canonical space-joined text
// ac_vibe_pattern_parse would itself accept and round-trip exactly -- the
// duration ratio is always emitted in ONE canonical spelling regardless of
// how it was originally typed (bare only ever appears here if len==0
// overall; a segment's own N/D is spelled "N" when D==1, "/D" when N==1, or
// "N/D" otherwise, mirroring how octave-marker runs are already always
// canonically re-emitted regardless of input spelling). Writes into buf
// (size n); empty string (not "0 segments") if len == 0.
void ac_vibe_pattern_format(char *buf, size_t n, const uint8_t *pattern, uint8_t len);

// Parses a custom SOUND pattern string: like ac_vibe_pattern_parse, but
// every on-token MUST carry a pitch -- an optional accidental prefix ('^'
// sharp or '_' flat, real ABC notation; no prefix = natural), then exactly
// one note letter from AC_NOTE_LETTER, then optional octave markers, then
// the same optional duration-ratio suffix ac_vibe_pattern_parse uses (e.g.
// "^C/2", "_D,,,3", "E"). A rest is still just 'z' plus the optional
// duration suffix, with no letter/accidental/octave -- 'z' followed
// directly by an accidental or letter is malformed, since a rest has no
// pitch. Octave markers, unchanged from before: one or more apostrophes
// "'" (right after the letter) shift up an octave each, one or more commas
// "," shift down an octave each, real ABC-notation convention (e.g. "C'"
// up 1, "C,,," down 3); no marker means AC_NOTE_OCTAVE_SAME. Up is capped
// at AC_NOTE_OCTAVE_MAX_UP (1) and down at AC_NOTE_OCTAVE_MAX_DOWN (8) --
// see that constant's own doc comment for why the range is asymmetric;
// exceeding either cap, or mixing "'" and "," within one token, is
// malformed. On success, writes one packed duration/rest byte per segment
// into out_flags (same encoding as ac_vibe_pattern_parse) and the packed
// octave/note/accidental value (see AC_NOTE_COUNT's doc comment) into
// out_notes (capacity max_segments each; a rest segment's out_notes entry
// is written as 0 but is meaningless -- callers must check the rest bit
// first), and the count into *out_len, returning true. Returns false
// (leaving every output untouched) on any malformed token (missing/invalid
// letter on an on-token, an accidental with no following letter, more than
// one accidental character, an accidental on a rest, an unrecognized
// letter, an octave marker with no preceding letter, mixed markers, a
// marker run past its cap, or a malformed duration ratio as described
// above), an empty/whitespace-only string, or more than max_segments
// tokens.
bool ac_sound_pattern_parse(const char *text, uint8_t max_segments,
                             uint8_t *out_flags, uint8_t *out_notes, uint8_t *out_len);

// Formats a parsed sound pattern back into the canonical space-joined text
// ac_sound_pattern_parse would itself accept and round-trip exactly --
// accidental prefix, letter, octave markers, then the same canonical
// duration-ratio suffix ac_vibe_pattern_format's own doc comment describes.
// Writes into buf (size n); empty string (not "0 segments") if len == 0.
void ac_sound_pattern_format(char *buf, size_t n, const uint8_t *flags, const uint8_t *notes, uint8_t len);

// Computes one segment's real duration in milliseconds from its packed
// AC_PATTERN_DURATION_MASK payload (`round(AC_NOTE_DEFAULT_MS * N / D)`) --
// the single shared place this arithmetic lives, used by every ring-time/
// preview/duration-summing call site in main.c instead of each one
// recomputing it inline.
uint32_t ac_pattern_duration_ms(uint8_t payload);

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
