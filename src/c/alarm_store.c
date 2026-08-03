// SPDX-License-Identifier: GPL-3.0-only
#include <pebble.h>
#include "alarm_store.h"
#include <string.h>

// Historical byte-size of `Alarm` at each schema version this migration can
// forward-read from -- see STORE_SCHEMA's comment (alarm_store.h) for the
// ground rules a future struct change needs to follow to add its own entry
// here. Only defined from the version this migration first shipped in (7)
// onward; schema <=6 predates it and still wipes (falls through to
// `default: return 0`, same as a declared hard break does).
//
// These are frozen literals captured by hand at the moment each becomes
// historical -- deliberately NOT `sizeof(Alarm)`, which always reflects
// whatever the CURRENT struct looks like and would silently return the
// wrong (larger) size for every entry here once Alarm grows again.
static size_t alarm_size_for_schema(int schema) {
  switch (schema) {
    case 7: return 184;   // baseline this migration shipped in
    case 8: return 184;   // + increasing_volume (bool) -- fits into existing
                           // trailing struct padding, so the byte size is
                           // unchanged from schema 7
    case 9: return 248;   // + cron_dom_mask (uint32) + cron_month_mask (uint16)
                           // + cron_dom/cron_month (CRON_FIELD_LEN each) --
                           // frozen sizeof(Alarm) captured at the moment
                           // schema became 9, for forward migration from it
    case 10: return 248;  // NOT 288 -- see STORE_SCHEMA's comment
                           // (alarm_store.h) for the full story: schema 10
                           // was SUPPOSED to add cron_week_mask/cron_week (a
                           // real 288-byte struct), but persist_write_data's
                           // 256B/key cap silently rejected every attempt to
                           // write that, so no schema-10 install's on-disk
                           // main blob was ever actually bigger than
                           // schema 9's. This entry reflects on-disk
                           // reality, not the (never-achieved) struct size.
    case 11: return 248;  // the schema-10 fix: cron_week_mask/cron_week moved
                           // to their own extension key (see store_load/
                           // store_save below), so the frozen MAIN blob size
                           // is unchanged from schema 9/10.
    default: return 0;
  }
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
_Static_assert(ALARM_MAIN_SIZE <= PERSIST_DATA_MAX_LENGTH,
               "Alarm's main persisted blob exceeds persist_write_data's per-key cap");
_Static_assert(ALARM_EXT_SIZE <= PERSIST_DATA_MAX_LENGTH,
               "Alarm's extension persisted blob exceeds persist_write_data's per-key cap");

// A stored schema older than STORE_SCHEMA is only trusted if
// alarm_size_for_schema() has an entry for it -- in that case,
// persist_read_data's own "copies min(buffer_size, actual_stored_size),
// leaves the rest of buffer untouched" behavior already does the right
// thing: reading an old, shorter blob into a zeroed current-sized Alarm
// (the memset below) naturally reproduces every field that existed back
// then correctly, leaving only the new trailing field(s) at 0. A newer-
// than-us stored schema (a downgrade) or one with no recorded size
// (predates this table, or a declared hard break) wipes, exactly like
// every schema mismatch did before this migration existed.
int store_load(Alarm *out) {
  if (!persist_exists(PERSIST_KEY_SCHEMA)) { return 0; }
  int stored_schema = persist_read_int(PERSIST_KEY_SCHEMA);
  bool migrating = false;
  if (stored_schema != STORE_SCHEMA) {
    if (stored_schema > STORE_SCHEMA) { return 0; }
    if (alarm_size_for_schema(stored_schema) == 0) { return 0; }
    migrating = true;
  }
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
      persist_read_data(PERSIST_KEY_ALARM_EXT_BASE + i, &out[i].cron_week_mask, ALARM_EXT_SIZE);
    } else if (out[i].is_cron) {
      out[i].cron_week_mask = AC_WEEK_ALL;
      strcpy(out[i].cron_week, "*");
    }
  }
  if (migrating) {
    // Mark the migration handled now, so a later load (before any real
    // mutation re-saves the alarms at full current size) doesn't re-check
    // or re-migrate -- persist_read_data above already tolerates the
    // per-alarm blobs staying physically old-sized on disk until then.
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
