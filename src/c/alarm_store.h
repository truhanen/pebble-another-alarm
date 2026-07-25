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
#define PERSIST_KEY_ALARM_BASE         100   // alarm i -> key 100+i (one Alarm per key)
#define STORE_SCHEMA 6   // bumped: Alarm gained cron fields (is_cron, cron_min/hour_mask,
                          // cron_last_fired_min, cron_min/hour/dow strings), then auto_stop

// Loads alarms into out (capacity MAX_ALARMS); returns count, or 0 if none/old schema.
int store_load(Alarm *out);
// Persists `count` alarms (one per key) + schema + count.
void store_save(const Alarm *a, int count);

// Wakeup id (-1 when none).
int32_t store_load_wakeup_id(void);
void store_save_wakeup_id(int32_t id);

// First day of week for the weekday picker / repeat summary (0=Sunday,
// 1=Monday; defaults to 1/Monday when unset).
int store_load_first_day_of_week(void);
void store_save_first_day_of_week(int day);

// Vibration pattern for the ring screen (0=Double, 1=Short, 2=Long; defaults
// to 0/Double when unset).
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
