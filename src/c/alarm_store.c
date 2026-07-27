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
    default: return 0;
  }
}

// One Alarm per persist key (PERSIST_KEY_ALARM_BASE+i), same rationale as
// timer_store.c: persist_write_data caps at 256B/key, so MAX_ALARMS Alarms
// packed into one blob would risk exceeding that as the struct grows.
//
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
      persist_read_data(PERSIST_KEY_ALARM_BASE + i, &out[i], sizeof(Alarm));
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
    persist_write_data(PERSIST_KEY_ALARM_BASE + i, &a[i], sizeof(Alarm));
  }
  // drop any stale keys beyond the new count
  for (int i = count; i < MAX_ALARMS; i++) {
    if (persist_exists(PERSIST_KEY_ALARM_BASE + i)) { persist_delete(PERSIST_KEY_ALARM_BASE + i); }
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
  if (!persist_exists(PERSIST_KEY_AUDIO_VOLUME)) { return 0; }   // default: sound disabled
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
