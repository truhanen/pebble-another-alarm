// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "alarm_calc.h"
#include <stdint.h>

#define PERSIST_KEY_SCHEMA               1
#define PERSIST_KEY_COUNT                2
#define PERSIST_KEY_WAKEUPID             3
#define PERSIST_KEY_FIRST_DAY_OF_WEEK    4   // 0=Sunday, 1=Monday
#define PERSIST_KEY_VIBE_PATTERN         5   // 0=Double, 1=Short, 2=Long
#define PERSIST_KEY_DEFAULT_SNOOZE_MINUTES 6
#define PERSIST_KEY_DEFAULT_SNOOZE_MAX   7
#define PERSIST_KEY_NEXT_LOCAL_ID        8
#define PERSIST_KEY_AUDIO_VOLUME         9   // 0-100, 0 = sound disabled (matches Instant Timer's audioVolume)
#define PERSIST_KEY_DEFAULT_VIBRATION_ENABLED 10
#define PERSIST_KEY_DEFAULT_SOUND_ENABLED     11
#define PERSIST_KEY_DEFAULT_INCREASING_VOLUME 12
#define PERSIST_KEY_DATE_FORMAT          13   // 0=Day.Month ("1.6."), 1=Month/Day ("6/1")
// Custom-tune library -- a separate persisted table from Alarm/STORE_SCHEMA
// below, with its own schema counter (TUNE_STORE_SCHEMA) since it's a
// logically independent concern: a future Alarm-only schema bump shouldn't
// force wiping the tune library, and vice versa. See store_load_tunes()/
// store_save_tunes() in alarm_store.c.
#define PERSIST_KEY_TUNE_SCHEMA          14
#define PERSIST_KEY_TUNE_COUNT           15
#define PERSIST_KEY_NEXT_TUNE_ID         16
#define TUNE_STORE_SCHEMA 3
// Bumped from 2: pattern_note[]'s octave encoding widened from 3 values
// (down 1/same/up 1) to 10 (down up to 8/same/up 1), to support stacking
// multiple "'"/"," markers -- see AC_NOTE_OCTAVE_MAX_UP/_MAX_DOWN's own doc
// comment (alarm_calc.h). Same "byte VALUE means something different now"
// category as Alarm's own schema-18 bump below; store_load_tunes() wipes on
// any mismatch anyway, so this needed nothing beyond the bump itself.
//
// Bumped from 1 to 2: pattern[]/pattern_note[] grew from AC_PATTERN_MAX_SEGMENTS
// (16) to AC_SOUND_PATTERN_MAX_SEGMENTS (32) -- a resize, not an append, so
// (per the ground rules below) this is a hard break; store_load_tunes()
// wipes on any schema mismatch anyway, so this needed nothing beyond the
// bump itself.
//
// Ground rules for growing CustomTune (alarm_calc.h), mirroring STORE_SCHEMA's
// own rules below: append new fields at the very end only, never insert/
// reorder/resize/remove; if a change genuinely isn't additive, just bump
// TUNE_STORE_SCHEMA (store_load_tunes() wipes on any mismatch -- there's no
// per-version size table for this table at all, unlike Alarm's own
// alarm_size_for_schema(), since every mismatch here wipes regardless).
#define PERSIST_KEY_ALARM_BASE         100   // alarm i -> key 100+i (one Alarm per key)
// alarm i's trailing fields (from cron_week_mask onward -- see the
// PERSIST_DATA_MAX_LENGTH note below) -> key 200+i. Kept well clear of
// PERSIST_KEY_ALARM_BASE's own 100..115 range (MAX_ALARMS=16) with room to
// grow.
#define PERSIST_KEY_ALARM_EXT_BASE     200
// Tune i -> key 300+i (one CustomTune per key, no main/extension split
// needed -- sizeof(CustomTune) is well under PERSIST_DATA_MAX_LENGTH).
// Clear of PERSIST_KEY_ALARM_BASE(100-115)/PERSIST_KEY_ALARM_EXT_BASE(200-215).
#define PERSIST_KEY_TUNE_BASE          300
#define STORE_SCHEMA 19  // bumped: removed sound's inline-Custom representation
                          // (sound_pattern_len/sound_pattern[]/sound_pattern_note[]) --
                          // tracing every call site that could set sound_pattern_kind==1
                          // found exactly one left (main.c's Tune-preset direct-commit
                          // path), and it only ever copies a SOUND_PRESETS[] entry's own
                          // bytes verbatim -- never genuinely freehand (sound's own
                          // "type a custom pattern" path was already replaced by the tune
                          // library, kind==2, earlier this dev cycle). So the byte arrays
                          // were 100% redundant with an index; replaced with `sound_preset`
                          // (alarm_calc.h), mirroring vibe_pattern's own index-into-a-
                          // preset-table approach. `sound_pattern_kind` narrows to
                          // {0 = Preset (sound_preset), 2 = Library (custom_tune_id)} --
                          // 1 is retired.
                          //
                          // A genuine field removal, not an append -- the fields removed
                          // sat in the MIDDLE of the EXTENSION blob's tail (between
                          // sound_pattern_kind and vibe_pattern_kind, and again between
                          // vibe_pattern_custom[] and custom_tune_id), so every field
                          // after them shifts offset. This is a materially different kind
                          // of hard break than 13/14/15/17/18 below, which only ever
                          // reinterpreted or resized bytes without changing anyone ELSE's
                          // offset: it invalidates a full-blob byte-copy from EVERY prior
                          // schema's EXTENSION data, not just the ones already flagged as
                          // hard breaks. store_load() (alarm_store.c) handles this by
                          // decoupling EXTENSION blob trust from alarm_size_for_schema()
                          // entirely: that table now only gates the MAIN blob (whose own
                          // layout has never once changed across this whole history, so
                          // it now has an entry for every schema 7 onward, including
                          // 16/17/18 -- see its own updated comment). The EXTENSION blob
                          // instead gets its OWN independent range check
                          // (ext_schema_is_appendable(), alarm_store.c), starting at 19 --
                          // any stored schema from 19 onward (not a downgrade) is read in
                          // FULL up to the current ALARM_EXT_SIZE, so a future pure append
                          // to this blob (a new trailing field, nothing resized/reordered/
                          // removed) survives migration completely, the same free ride
                          // MAIN's own forward migration has always had. Only a stored
                          // schema OLDER than 19 (a formerly-"recognized" one like 12-15,
                          // or anything that predates this tracking) or a downgrade this
                          // binary has never seen reads only its trusted leading
                          // `ALARM_EXT_CORE_SIZE` bytes (cron_week_mask/cron_week) and
                          // defaults everything after. This is also what finally makes
                          // this codebase's long-standing wipe-on-any-hard-break behavior
                          // go away for good: a schedule (label/time/repeat/cron,
                          // including the week-of-year restriction) now survives ANY
                          // future signal-only hard break intact, a downgrade no longer
                          // wipes unconditionally either, and an ordinary signal-only
                          // APPEND no longer costs users their Tune/Vibration settings the
                          // way this schema's own hard break necessarily does, once. See
                          // store_load()'s own comment (alarm_store.c) for the full
                          // mechanism.
                          //
                          // Prior bumps (12 through 18): a single, never-published dev cycle
                          // iterating on the custom sound/vibe signal-pattern encoding --
                          // Alarm gained sound_pattern_kind/len/pattern[] and
                          // vibe_pattern_kind/len/vibe_pattern_custom[] at 12, then the byte
                          // grammar for both was resized and reinterpreted four more times
                          // as the note/break/octave grammar evolved (a packed
                          // bit7=break/bits0-6=duration scheme at 13, sparse duration digits
                          // at 14, real ABC-notation octave markers at 15, the
                          // custom-tune-library's custom_tune_id appended cleanly at 16,
                          // sound/vibe pattern caps split into separate sizes -- a genuine
                          // resize, not just an append -- at 17, and stacked octave markers
                          // widening the note encoding again at 18). No published version was
                          // ever on schema 12-18 -- these bumps only ever shipped on this
                          // project's own dev/test builds, so there's no real user this
                          // history protects. It survives here purely as the record of two
                          // hard lessons that directly shaped schema 19's design above:
                          //  - alarm_size_for_schema() used to be an enumerated per-version
                          //    switch, and deliberately had NO entry for 16 or 17 (both
                          //    genuine byte-reinterpretation/resize hard breaks) -- correct
                          //    in isolation, but it meant crossing either boundary wiped the
                          //    WHOLE alarm, schedule included, which is the repeated
                          //    real-world alarm-list wipe that prompted this whole redesign.
                          //    That table is now a permanent range check instead (see its own
                          //    comment, alarm_store.c), and MAIN/EXTENSION trust decisions
                          //    were split apart entirely so a signal-only break can never
                          //    touch the schedule again.
                          //  - Sound's inline custom-pattern bytes turned out to be 100% dead
                          //    weight by the time schema 19 shipped: every remaining call
                          //    site that could still set sound_pattern_kind==1 only ever
                          //    copied a SOUND_PRESETS[] entry's own bytes verbatim, never
                          //    anything genuinely freehand, once the tune library replaced
                          //    sound's own "type a custom pattern" path. Worth remembering
                          //    next time a field looks load-bearing: trace every call site
                          //    before assuming it still needs its own storage.
                          //
                          // Prior bump (11): Alarm gained cron fields (is_cron, cron_min/hour_mask,
                          // cron_last_fired_min, cron_min/hour/dow strings), then auto_dismiss,
                          // then vibe_pattern (per-alarm vibration pattern override), then
                          // increasing_volume (per-alarm ramp-up-to-max toggle), then cron
                          // day-of-month/month fields (cron_dom_mask, cron_month_mask,
                          // cron_dom, cron_month) for cron mode's 5-field extension, then
                          // cron_week_mask/cron_week (ISO week-of-year) for the 6-field
                          // extension (schema 10), then THIS bump: splitting
                          // cron_week_mask/cron_week off of the main per-alarm blob into
                          // their own persist key (PERSIST_KEY_ALARM_EXT_BASE).
                          //
                          // Why: sizeof(Alarm) had grown to 288 bytes by schema 10, but
                          // persist_write_data() caps at PERSIST_DATA_MAX_LENGTH (256) bytes
                          // per key -- a limit this file already warned about (see
                          // store_save()'s own comment) without anyone re-checking it against
                          // the struct's actual current size. On real firmware, a
                          // persist_write_data() call over that cap is rejected outright
                          // (nothing written), while store_save()'s separate
                          // persist_write_int(PERSIST_KEY_SCHEMA, ...) call succeeds
                          // unconditionally -- so every schema-10 install silently kept
                          // writing its PERSIST_KEY_SCHEMA marker as 10 while every actual
                          // per-alarm blob write failed and the on-disk data stayed frozen at
                          // whatever the last successful (schema-9-sized, 248-byte) blob was.
                          // Since stored_schema == STORE_SCHEMA on the next load, the
                          // migration branch that would've defaulted cron_week_mask/cron_week
                          // never ran either -- so any cron alarm's week field silently came
                          // back empty (and invalid) after every relaunch. Not a display bug:
                          // real, silent data loss, and it would have kept happening for any
                          // schema-10 install indefinitely.
                          //
                          // The fix moves cron_week_mask/cron_week (the tail 40 bytes) to
                          // their own small key per alarm instead of shrinking any existing
                          // field: the main blob (PERSIST_KEY_ALARM_BASE+i) is capped at
                          // exactly `offsetof(Alarm, cron_week_mask)` (248 bytes -- happens to
                          // equal schema 9's own frozen size, since nothing before
                          // cron_week_mask has changed since then) and stays frozen there
                          // forever; the trailing extension blob
                          // (PERSIST_KEY_ALARM_EXT_BASE+i) covers everything from
                          // cron_week_mask to the end of the struct, currently 40 bytes with
                          // ~216 bytes of headroom before it would need splitting again. Any
                          // FUTURE field appended past cron_week_mask (ground rule #1 below)
                          // automatically lands in this extension blob, not the frozen main
                          // one -- see store_load()/store_save() in alarm_store.c, and the
                          // _Static_assert there that fails the build if either blob ever
                          // grows past PERSIST_DATA_MAX_LENGTH again.
                          //
                          // The cron_dom_mask/cron_month_mask/cron_week_mask bumps need more
                          // than just a size-table entry: a freshly zero-filled mask on
                          // migration means "matches nothing", unlike increasing_volume's
                          // 0=off (already the right default) -- store_load() explicitly
                          // re-fills these to AC_DOM_ALL/AC_MONTH_ALL/AC_WEEK_ALL (and
                          // cron_dom/cron_month/cron_week to "*") for any migrated is_cron
                          // alarm, or every existing cron alarm's matching would silently die
                          // the instant this shipped. cron_week_mask/cron_week's own default-
                          // fill is no longer gated on the overall schema migration flag (see
                          // above for why that flag can't be trusted for this field
                          // specifically) -- it now runs whenever an alarm's extension key is
                          // simply missing, regardless of stored_schema.
                          //
                          // Every field added to Alarm so far (including vibe_pattern and
                          // increasing_volume) was appended at the very end of the struct,
                          // on purpose: it's what
                          // lets store_load()'s forward-compatible migration (see
                          // alarm_size_for_schema() in alarm_store.c) read an older, shorter
                          // blob's bytes into the front of a same-or-larger buffer and get
                          // correct values for every field that existed back then, with only
                          // the new trailing fields left at 0.
                          //
                          // Ground rules for the NEXT bump, to keep that working:
                          //  1. Add the new field(s) at the very end of the struct. Never
                          //     insert, reorder, resize, or remove an existing field -- any
                          //     of those is a hard break (see #3).
                          //  2. If your new field lands in the EXTENSION blob (after
                          //     cron_week_mask/cron_week -- true for any signal-pattern
                          //     field, which is everywhere new fields have landed since
                          //     schema 12) and is a genuine APPEND (nothing existing
                          //     resized/reordered/removed), neither `alarm_size_for_schema()`
                          //     NOR `ext_schema_is_appendable()` (both alarm_store.c) need
                          //     any change at all -- MAIN is untouched by definition, and
                          //     ext_schema_is_appendable()'s existing `>= 19` already covers
                          //     this new bump too, so store_load() reads the new field in
                          //     full for free via the same truncation trick MAIN has always
                          //     used. Just check the new EXTENSION size against
                          //     PERSIST_DATA_MAX_LENGTH (256) -- if it would now exceed that,
                          //     it needs splitting again the same way schema 10->11 did. If
                          //     your new field somehow lands in MAIN instead, that's new
                          //     territory (hasn't happened since schema 9) --
                          //     `alarm_size_for_schema()` would need a real new range
                          //     boundary added, not just a bump.
                          //  3. If the change genuinely can't be additive (a field removed,
                          //     reordered, or resized) but stays confined to the EXTENSION
                          //     blob (i.e. after cron_week_mask/cron_week, which is where
                          //     every signal-pattern field lives), `alarm_size_for_schema()`
                          //     still needs no change (MAIN is unaffected by definition), but
                          //     `ext_schema_is_appendable()`'s threshold DOES need bumping to
                          //     your new schema number -- otherwise the next release would
                          //     wrongly treat your broken layout as a safe append and
                          //     misread it. Once bumped, the EXTENSION fallback (below)
                          //     automatically activates for anything older than your new
                          //     threshold, with no other code change needed. If a future
                          //     change somehow needs to touch the MAIN blob itself
                          //     non-additively (hasn't happened yet, and shouldn't need to
                          //     for a long while, but say so here if it ever does), THAT'S
                          //     the case that would need its own new handling, since MAIN
                          //     currently has no fallback at all -- it's either fully
                          //     trusted or the whole load returns 0.
                          //
                          // The EXTENSION mechanism itself (store_load(), alarm_store.c) is
                          // now a range check, same shape as alarm_size_for_schema() but
                          // independent of it: `ext_schema_is_appendable(stored_schema)`
                          // (currently `>= 19`) decides, and a downgrade (stored_schema >
                          // STORE_SCHEMA) always counts as "no" regardless of that check,
                          // since a newer binary's own EXTENSION layout is unknown to us.
                          //
                          // When it says yes: read the EXTENSION blob in full, up to the
                          // CURRENT `ALARM_EXT_SIZE` -- an older-but-appendable blob simply
                          // doesn't have that many bytes stored yet, and
                          // `persist_read_data`'s own `min(buffer_size, actual_stored_size)`
                          // truncation means only however many bytes it actually holds get
                          // copied, leaving any newer trailing field(s) at their safe zero
                          // default from the memset above -- the exact same free ride
                          // MAIN's own forward migration has always had. A genuine future
                          // EXTENSION-only append (ground rule #2 above) costs nothing: no
                          // new code, and no signal-setting reset for existing users either.
                          //
                          // When it says no (a genuine EXTENSION-layout hard break, or a
                          // downgrade this binary has never seen): read the MAIN blob in
                          // full regardless (its own layout is untouched by definition --
                          // see alarm_size_for_schema()'s own comment) plus just
                          // `ALARM_EXT_CORE_SIZE` bytes from the FRONT of the EXTENSION blob
                          // -- `cron_week_mask`/`cron_week`, the first two fields ever
                          // appended into that blob (schema 10/11), never moved since.
                          // Every other EXTENSION field (`sound_pattern_kind`,
                          // `vibe_pattern_kind`, `vibe_pattern_len`, `vibe_pattern_custom[]`,
                          // `custom_tune_id`, `sound_preset`) resets to its safe zero
                          // default instead of being read at all -- the same truncation
                          // property, just applied to a smaller requested size this time.
                          //
                          // This is what lets a schedule (label/time/repeat/cron, including
                          // the week-of-year restriction) survive a signal-only hard break
                          // intact, rather than the whole alarm disappearing, AND lets an
                          // ordinary signal-only append no longer cost existing users their
                          // Tune/Vibration settings at all -- see this file's own git
                          // history/CLAUDE.md for the incident that prompted the first half
                          // of this (repeated alarm-list wipes purely from iterating on the
                          // sound/vibe pattern encoding during dev testing).

// Loads alarms into out (capacity MAX_ALARMS); returns count, or 0 if none/old schema.
int store_load(Alarm *out);
// Persists `count` alarms (one per key) + schema + count.
void store_save(const Alarm *a, int count);

// Loads custom tunes into out (capacity MAX_CUSTOM_TUNES); returns count, or
// 0 if none/schema mismatch (wipes on any mismatch -- see TUNE_STORE_SCHEMA).
int store_load_tunes(CustomTune *out);
// Persists `count` tunes (one per key) + schema + count.
void store_save_tunes(const CustomTune *tunes, int count);
// Allocates and persists the next tune id (starts at 1; 0 is reserved for
// "none"), mirroring store_load_next_local_id/store_save_next_local_id's
// shape for Alarm.id.
uint32_t store_alloc_tune_id(void);

// Wakeup id (-1 when none).
int32_t store_load_wakeup_id(void);
void store_save_wakeup_id(int32_t id);

// First day of week for the weekday picker / repeat summary (0=Sunday,
// 1=Monday; defaults to 1/Monday when unset).
int store_load_first_day_of_week(void);
void store_save_first_day_of_week(int day);

// Default vibration pattern (0=Double, 1=Short, 2=Long) copied into a new
// alarm's own vibe_pattern at creation (defaults to 0/Double when unset) --
// not read at ring time; each alarm rings with its own vibe_pattern.
int store_load_vibe_pattern(void);
void store_save_vibe_pattern(int pattern);

// Default snooze duration (minutes) pre-filled for newly created alarms
// (defaults to 9 when unset).
int store_load_default_snooze_minutes(void);
void store_save_default_snooze_minutes(int minutes);

// Default max snooze count pre-filled for newly created alarms (defaults to
// 3 when unset; 0 = unlimited).
int store_load_default_snooze_max(void);
void store_save_default_snooze_max(int max_count);

// Counter for the next watch-assigned Alarm.id (defaults to 1 when unset).
uint16_t store_load_next_local_id(void);
void store_save_next_local_id(uint16_t v);

// Global alarm sound volume, 0-100 (defaults to 0/disabled when unset, same
// as Instant Timer's audioVolume — an alarm's own sound_enabled toggle is an
// additional gate on top of this, not a replacement for it).
int store_load_audio_volume(void);
void store_save_audio_volume(int volume);

// Default vibration/sound toggles pre-filled for newly created alarms
// (phone-configured, "Defaults for new alarms" checkbox group; both default
// to true/enabled when unset — does not affect existing alarms).
bool store_load_default_vibration_enabled(void);
void store_save_default_vibration_enabled(bool enabled);
bool store_load_default_sound_enabled(void);
void store_save_default_sound_enabled(bool enabled);

// Default "increasing volume" toggle pre-filled for newly created alarms
// (phone-configured; defaults to false/off when unset — does not affect
// existing alarms). See Alarm.increasing_volume (alarm_calc.h).
bool store_load_default_increasing_volume(void);
void store_save_default_increasing_volume(bool enabled);

// Date format used by format_relative_fire_time()'s date-only tier (main.c)
// -- 0=Day.Month ("1.6."), 1=Month/Day ("6/1"); defaults to 0 when unset.
// Global, like AudioVolume: read live, not copied into anything per-alarm.
int store_load_date_format(void);
void store_save_date_format(int format);
