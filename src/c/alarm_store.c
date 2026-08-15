// SPDX-License-Identifier: GPL-3.0-only
#include <pebble.h>
#include "alarm_store.h"
#include <string.h>

// Historical byte-size of `Alarm`'s MAIN blob at each schema version this
// migration can forward-read from -- see STORE_SCHEMA's comment
// (alarm_store.h) for the ground rules a future struct change needs to
// follow to add its own entry here. Only defined from the version this
// migration first shipped in (7) onward; schema <=6 predates it and still
// wipes (falls through to `default: return 0`).
//
// These are frozen literals captured by hand at the moment each becomes
// historical -- deliberately NOT `sizeof(Alarm)`, which always reflects
// whatever the CURRENT struct looks like and would silently return the
// wrong (larger) size for every entry here once Alarm grows again.
//
// This table is now MAIN-blob-only -- it says nothing about the EXTENSION
// blob's compatibility, which store_load() below decides separately (see
// its own comment). That split matters because MAIN's own byte layout has
// never once changed across this entire history (184 bytes at schema 7-8,
// 248 from schema 9 onward), REGARDLESS of every signal-encoding rework
// that's happened in the EXTENSION blob (schemas 12 through 19, including
// two genuine hard breaks, 17 and 19) -- every one of those bumps left MAIN
// completely untouched. Given that, this is a plain RANGE check rather than
// a per-version switch/table: MAIN's size only ever depends on which of
// the two eras a schema falls in (7-8 vs. 9+), not on the specific version
// number, so there's no "did I remember to add a case for the new schema"
// step for a future bump to get wrong -- unlike the old enumerated table
// this replaced, which genuinely DID miss entries for 16/17/18 for exactly
// that reason (harmless at the time, since any hard break anywhere still
// wiped the whole alarm back then -- see STORE_SCHEMA's own comment,
// alarm_store.h, for the incident that prompted fixing this for good).
// A future change that DOES touch MAIN's own layout (hasn't happened yet)
// would need to actually add a real version boundary here, at which point
// this reverts to needing case-by-case entries again for schemas after it.
static size_t alarm_size_for_schema(int schema) {
  if (schema >= 9) { return 248; }
  if (schema >= 7) { return 184; }
  return 0;   // predates schema 7, when this migration first shipped
}

// EXTENSION's own equivalent of the range check above -- MAIN and EXTENSION
// each break on their own schedule (a resize/reorder/removal in one says
// nothing about the other), so this needs its own independent threshold
// rather than reusing alarm_size_for_schema()'s. Schema 19 is EXTENSION's
// baseline: it's the hard break that removed sound_pattern_len/
// sound_pattern[]/sound_pattern_note[] from the middle of this blob and
// added sound_preset in their place (see STORE_SCHEMA's comment,
// alarm_store.h) -- the first schema whose EXTENSION layout this binary
// trusts as a byte-compatible ancestor of the current one.
//
// Unlike alarm_size_for_schema(), a pure append AFTER this baseline (a new
// trailing field, nothing resized/reordered/removed) needs no new entry
// here at all: store_load() below always asks to read up to the CURRENT
// ALARM_EXT_SIZE bytes once this returns true, and persist_read_data's own
// `min(buffer_size, actual_stored_size)` truncation naturally reads only
// however many bytes an older blob actually holds, leaving whatever's
// newer at its safe zero default -- the exact same free-ride append
// handling alarm_size_for_schema() has always relied on for MAIN. Only a
// genuine FUTURE EXTENSION-layout hard break (a resize/reorder/removal, or
// -- as with 20's own bump -- a same-size REINTERPRETATION of what the
// bytes mean) needs to bump this threshold -- at which point anything
// older than the new hard break falls back to ALARM_EXT_CORE_SIZE-only,
// same as anything older than 20 does today.
static bool ext_schema_is_appendable(int schema) {
  return schema >= 20;
}

// The per-alarm blob is split across two persist keys, not one -- see
// STORE_SCHEMA's comment (alarm_store.h) for the full story. The MAIN blob
// (PERSIST_KEY_ALARM_BASE+i) covers everything up to (not including)
// cron_week_mask and is frozen at exactly that offset forever; the
// EXTENSION blob (PERSIST_KEY_ALARM_EXT_BASE+i) covers cron_week_mask
// through the end of the struct, so any future field appended after it (per
// the ground rules below) grows the extension blob, not the frozen main
// one. Both are asserted at compile time to stay under
// PERSIST_DATA_MAX_LENGTH.
#define ALARM_MAIN_SIZE (offsetof(Alarm, cron_week_mask))
#define ALARM_EXT_SIZE  (sizeof(Alarm) - offsetof(Alarm, cron_week_mask))
// The EXTENSION blob's own trusted PREFIX -- cron_week_mask/cron_week are
// the first two fields appended into this blob (schema 10/11) and have
// never moved since, regardless of how many times the signal-pattern
// fields AFTER them have resized/reinterpreted (12 through the current
// schema). store_load() falls back to reading only this many bytes of the
// EXTENSION key whenever the stored schema is NOT a byte-compatible
// ancestor of the current one (ext_schema_is_appendable() above says no --
// a genuine EXTENSION-layout hard break -- or it's a downgrade this binary
// has never seen) -- recovering the schedule-relevant week-of-year
// restriction even when the rest of that key's stored bytes are under some
// encoding this binary doesn't (or no longer) understands.
#define ALARM_EXT_CORE_SIZE (offsetof(Alarm, sound_pattern_kind) - offsetof(Alarm, cron_week_mask))
_Static_assert(ALARM_MAIN_SIZE <= PERSIST_DATA_MAX_LENGTH,
               "Alarm's main persisted blob exceeds persist_write_data's per-key cap");
_Static_assert(ALARM_EXT_SIZE <= PERSIST_DATA_MAX_LENGTH,
               "Alarm's extension persisted blob exceeds persist_write_data's per-key cap");

// MAIN is trusted (read in full) for any stored_schema this codebase has
// ever shipped (alarm_size_for_schema() nonzero) AND for any stored_schema
// NEWER than STORE_SCHEMA (a downgrade) -- betting that MAIN's own layout
// keeps being as stable going forward as it has been for its entire history
// so far (see alarm_size_for_schema()'s own comment). Only a stored schema
// OLDER than 7 (genuinely predating this migration system) wipes outright.
//
// EXTENSION is trusted up to its own CURRENT full size (ALARM_EXT_SIZE)
// whenever the stored schema is a byte-compatible ancestor of the current
// layout (ext_schema_is_appendable() above) AND it's not a downgrade --
// this is what makes an ordinary future append to the EXTENSION blob (a new
// trailing field, no resize/reorder/removal) survive migration fully, the
// same free ride MAIN's own forward migration has always gotten. Any other
// case -- a genuine EXTENSION-layout hard break (ext_schema_is_appendable()
// says no) or a downgrade this binary has never seen -- reads only the
// trusted ALARM_EXT_CORE_SIZE prefix (cron_week_mask/cron_week) and leaves
// every field after it (sound_pattern_kind onward) at its safe zero
// default. persist_read_data's own "copies min(buffer_size,
// actual_stored_size), leaves the rest of the buffer untouched" behavior is
// what makes both of these safe -- reading up to a larger current size than
// an older blob actually holds, or reading fewer bytes than a key might
// currently hold, both just work.
int store_load(Alarm *out) {
  if (!persist_exists(PERSIST_KEY_SCHEMA)) { return 0; }
  int stored_schema = persist_read_int(PERSIST_KEY_SCHEMA);
  bool migrating = (stored_schema != STORE_SCHEMA);
  if (migrating && stored_schema < STORE_SCHEMA && alarm_size_for_schema(stored_schema) == 0) {
    return 0;   // genuinely predates schema 7 -- MAIN's own layout is unknown
  }
  bool downgrade = stored_schema > STORE_SCHEMA;
  bool ext_appendable = !downgrade && ext_schema_is_appendable(stored_schema);
  int count = persist_exists(PERSIST_KEY_COUNT) ? persist_read_int(PERSIST_KEY_COUNT) : 0;
  if (count > MAX_ALARMS) { count = MAX_ALARMS; }
  if (count < 0) { count = 0; }
  for (int i = 0; i < count; i++) {
    memset(&out[i], 0, sizeof(Alarm));
    if (persist_exists(PERSIST_KEY_ALARM_BASE + i)) {
      persist_read_data(PERSIST_KEY_ALARM_BASE + i, &out[i], ALARM_MAIN_SIZE);
    }
    // cron_dom_mask/cron_month_mask are new in schema 9 -- a stored blob
    // older than that has them zero-filled by the memset above, which means
    // "matches nothing" rather than the intended "unrestricted" default.
    // Only cron alarms care (the fields are unused otherwise), but for one
    // of those, leaving them at 0 would silently and permanently stop its
    // matching the instant this schema bump ships.
    if (migrating && stored_schema < 9 && out[i].is_cron) {
      out[i].cron_dom_mask = AC_DOM_ALL;
      out[i].cron_month_mask = AC_MONTH_ALL;
      strcpy(out[i].cron_dom, "*");
      strcpy(out[i].cron_month, "*");
    }
    // cron_week_mask/cron_week live in their own extension key, never in the
    // main blob above, on ANY schema -- so their presence is checked
    // per-alarm rather than gated on the overall `migrating` flag (that
    // flag can't be trusted for this one field: see STORE_SCHEMA's comment
    // for why a schema-10 install's marker could say "up to date" while its
    // extension data was never actually written).
    if (persist_exists(PERSIST_KEY_ALARM_EXT_BASE + i)) {
      persist_read_data(PERSIST_KEY_ALARM_EXT_BASE + i, &out[i].cron_week_mask,
                         ext_appendable ? ALARM_EXT_SIZE : ALARM_EXT_CORE_SIZE);
    } else if (out[i].is_cron) {
      out[i].cron_week_mask = AC_WEEK_ALL;
      strcpy(out[i].cron_week, "*");
    }
    // Defensive sanity check on Core fields -- cheap, always run, not
    // gated on `migrating`: protects against ever trusting an out-of-range
    // value, regardless of whether it came from a downgrade (a schema this
    // binary has never seen) or plain corruption. sound_pattern_kind/
    // vibe_pattern_kind/vibe_pattern_len/custom_tune_id/sound_preset need
    // no equivalent check -- when `!ext_appendable` they're already at their
    // safe zero default from the memset above and were never read past
    // ALARM_EXT_CORE_SIZE to begin with.
    if (out[i].hour > 23) { out[i].hour = 0; }
    if (out[i].minute > 59) { out[i].minute = 0; }
    out[i].name[NAME_LEN] = '\0';
    if (out[i].repeat_days > AC_DAY_ALL) { out[i].repeat_days = 0; }
  }
  if (migrating) {
    // Mark the migration handled now, so a later load (before any real
    // mutation re-saves the alarms at full current size) doesn't re-check
    // or re-migrate -- persist_read_data above already tolerates the
    // per-alarm blobs staying physically old-sized (appendable case) or
    // differently-shaped past ALARM_EXT_CORE_SIZE (hard-break case) on disk
    // until then.
    persist_write_int(PERSIST_KEY_SCHEMA, STORE_SCHEMA);
  }
  return count;
}

void store_save(const Alarm *a, int count) {
  if (count > MAX_ALARMS) { count = MAX_ALARMS; }
  persist_write_int(PERSIST_KEY_SCHEMA, STORE_SCHEMA);
  persist_write_int(PERSIST_KEY_COUNT, count);
  for (int i = 0; i < count; i++) {
    persist_write_data(PERSIST_KEY_ALARM_BASE + i, &a[i], ALARM_MAIN_SIZE);
    persist_write_data(PERSIST_KEY_ALARM_EXT_BASE + i, &a[i].cron_week_mask, ALARM_EXT_SIZE);
  }
  // drop any stale keys beyond the new count
  for (int i = count; i < MAX_ALARMS; i++) {
    if (persist_exists(PERSIST_KEY_ALARM_BASE + i)) { persist_delete(PERSIST_KEY_ALARM_BASE + i); }
    if (persist_exists(PERSIST_KEY_ALARM_EXT_BASE + i)) { persist_delete(PERSIST_KEY_ALARM_EXT_BASE + i); }
  }
}

int32_t store_load_wakeup_id(void) {
  if (!persist_exists(PERSIST_KEY_WAKEUPID)) { return -1; }
  return persist_read_int(PERSIST_KEY_WAKEUPID);
}

void store_save_wakeup_id(int32_t id) {
  persist_write_int(PERSIST_KEY_WAKEUPID, id);
}

int store_load_first_day_of_week(void) {
  if (!persist_exists(PERSIST_KEY_FIRST_DAY_OF_WEEK)) { return 1; }   // default Monday
  return persist_read_int(PERSIST_KEY_FIRST_DAY_OF_WEEK);
}

void store_save_first_day_of_week(int day) {
  persist_write_int(PERSIST_KEY_FIRST_DAY_OF_WEEK, day);
}

int store_load_date_format(void) {
  if (!persist_exists(PERSIST_KEY_DATE_FORMAT)) { return 0; }   // default Day.Month
  return persist_read_int(PERSIST_KEY_DATE_FORMAT);
}

void store_save_date_format(int format) {
  persist_write_int(PERSIST_KEY_DATE_FORMAT, format);
}

int store_load_vibe_pattern(void) {
  if (!persist_exists(PERSIST_KEY_VIBE_PATTERN)) { return 0; }   // default Double
  return persist_read_int(PERSIST_KEY_VIBE_PATTERN);
}

void store_save_vibe_pattern(int pattern) {
  persist_write_int(PERSIST_KEY_VIBE_PATTERN, pattern);
}

int store_load_default_snooze_minutes(void) {
  if (!persist_exists(PERSIST_KEY_DEFAULT_SNOOZE_MINUTES)) { return 9; }
  return persist_read_int(PERSIST_KEY_DEFAULT_SNOOZE_MINUTES);
}

void store_save_default_snooze_minutes(int minutes) {
  persist_write_int(PERSIST_KEY_DEFAULT_SNOOZE_MINUTES, minutes);
}

int store_load_default_snooze_max(void) {
  if (!persist_exists(PERSIST_KEY_DEFAULT_SNOOZE_MAX)) { return 3; }
  return persist_read_int(PERSIST_KEY_DEFAULT_SNOOZE_MAX);
}

void store_save_default_snooze_max(int max_count) {
  persist_write_int(PERSIST_KEY_DEFAULT_SNOOZE_MAX, max_count);
}

uint16_t store_load_next_local_id(void) {
  if (!persist_exists(PERSIST_KEY_NEXT_LOCAL_ID)) { return 1; }
  return (uint16_t)persist_read_int(PERSIST_KEY_NEXT_LOCAL_ID);
}

void store_save_next_local_id(uint16_t v) {
  persist_write_int(PERSIST_KEY_NEXT_LOCAL_ID, v);
}

int store_load_audio_volume(void) {
  // Matches config_clay.ts's AudioVolume slider defaultValue, so a fresh
  // install rings audibly out of the box even if the phone config page is
  // never opened -- an alarm's own sound_enabled defaults to true, and a
  // 0 here would silently defeat that regardless of the per-alarm toggle.
  if (!persist_exists(PERSIST_KEY_AUDIO_VOLUME)) { return 50; }
  return persist_read_int(PERSIST_KEY_AUDIO_VOLUME);
}

void store_save_audio_volume(int volume) {
  persist_write_int(PERSIST_KEY_AUDIO_VOLUME, volume);
}

bool store_load_default_vibration_enabled(void) {
  if (!persist_exists(PERSIST_KEY_DEFAULT_VIBRATION_ENABLED)) { return true; }
  return persist_read_int(PERSIST_KEY_DEFAULT_VIBRATION_ENABLED) != 0;
}

void store_save_default_vibration_enabled(bool enabled) {
  persist_write_int(PERSIST_KEY_DEFAULT_VIBRATION_ENABLED, enabled ? 1 : 0);
}

bool store_load_default_sound_enabled(void) {
  if (!persist_exists(PERSIST_KEY_DEFAULT_SOUND_ENABLED)) { return true; }
  return persist_read_int(PERSIST_KEY_DEFAULT_SOUND_ENABLED) != 0;
}

void store_save_default_sound_enabled(bool enabled) {
  persist_write_int(PERSIST_KEY_DEFAULT_SOUND_ENABLED, enabled ? 1 : 0);
}

bool store_load_default_increasing_volume(void) {
  if (!persist_exists(PERSIST_KEY_DEFAULT_INCREASING_VOLUME)) { return false; }
  return persist_read_int(PERSIST_KEY_DEFAULT_INCREASING_VOLUME) != 0;
}

void store_save_default_increasing_volume(bool enabled) {
  persist_write_int(PERSIST_KEY_DEFAULT_INCREASING_VOLUME, enabled ? 1 : 0);
}

_Static_assert(sizeof(CustomTune) <= PERSIST_DATA_MAX_LENGTH,
               "CustomTune's persisted blob exceeds persist_write_data's per-key cap");

// Brand-new table (TUNE_STORE_SCHEMA 1 is the only version that has ever
// existed), so unlike store_load() there's no per-version size table to
// forward-migrate from yet -- any schema mismatch at all just wipes, same
// as store_load() does for an unrecognized/downgraded STORE_SCHEMA.
int store_load_tunes(CustomTune *out) {
  if (!persist_exists(PERSIST_KEY_TUNE_SCHEMA)) { return 0; }
  if (persist_read_int(PERSIST_KEY_TUNE_SCHEMA) != TUNE_STORE_SCHEMA) { return 0; }
  int count = persist_exists(PERSIST_KEY_TUNE_COUNT) ? persist_read_int(PERSIST_KEY_TUNE_COUNT) : 0;
  if (count > MAX_CUSTOM_TUNES) { count = MAX_CUSTOM_TUNES; }
  if (count < 0) { count = 0; }
  for (int i = 0; i < count; i++) {
    memset(&out[i], 0, sizeof(CustomTune));
    if (persist_exists(PERSIST_KEY_TUNE_BASE + i)) {
      persist_read_data(PERSIST_KEY_TUNE_BASE + i, &out[i], sizeof(CustomTune));
    }
  }
  return count;
}

void store_save_tunes(const CustomTune *tunes, int count) {
  if (count > MAX_CUSTOM_TUNES) { count = MAX_CUSTOM_TUNES; }
  persist_write_int(PERSIST_KEY_TUNE_SCHEMA, TUNE_STORE_SCHEMA);
  persist_write_int(PERSIST_KEY_TUNE_COUNT, count);
  for (int i = 0; i < count; i++) {
    persist_write_data(PERSIST_KEY_TUNE_BASE + i, &tunes[i], sizeof(CustomTune));
  }
  for (int i = count; i < MAX_CUSTOM_TUNES; i++) {
    if (persist_exists(PERSIST_KEY_TUNE_BASE + i)) { persist_delete(PERSIST_KEY_TUNE_BASE + i); }
  }
}

uint32_t store_alloc_tune_id(void) {
  uint32_t next = persist_exists(PERSIST_KEY_NEXT_TUNE_ID) ? (uint32_t)persist_read_int(PERSIST_KEY_NEXT_TUNE_ID) : 1;
  persist_write_int(PERSIST_KEY_NEXT_TUNE_ID, (int)(next + 1));
  return next;
}
