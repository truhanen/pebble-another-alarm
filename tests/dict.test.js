const test = require('node:test');
const assert = require('node:assert');
const { buildDict, getDefaultClaySettings, migrateSavedSettings, flattenAlarmDefaults } = require('../src/pkjs/dict');
const clayConfig = require('../src/pkjs/config_clay').default;

test('buildDict parses Clay string values to ints', () => {
  assert.deepStrictEqual(buildDict({
    FirstDayOfWeek: '0',
    DateFormat: '1',
    AlarmVibePattern: '2',
    DefaultSnoozeMinutes: '15',
    DefaultSnoozeMax: '5',
    AudioVolume: '40',
  }), {
    FirstDayOfWeek: 0,
    DateFormat: 1,
    AlarmVibePattern: 2,
    DefaultTuneId: 0, DefaultTunePreset: 0, DefaultVibeId: 0,
    DefaultSnoozeMinutes: 15,
    DefaultSnoozeMax: 5,
    DefaultSoundEnabled: 1,
    DefaultVibrationEnabled: 1,
    DefaultIncreasingVolume: 0,
    AudioVolume: 40,
  });
});

test('buildDict accepts AudioVolume as a number (Clay slider type)', () => {
  assert.deepStrictEqual(buildDict({ AudioVolume: 75 }), {
    FirstDayOfWeek: 1,
    DateFormat: 0,
    AlarmVibePattern: 0,
    DefaultTuneId: 0, DefaultTunePreset: 0, DefaultVibeId: 0,
    DefaultSnoozeMinutes: 9,
    DefaultSnoozeMax: 3,
    DefaultSoundEnabled: 1,
    DefaultVibrationEnabled: 1,
    DefaultIncreasingVolume: 0,
    AudioVolume: 75,
  });
});

test('buildDict defaults missing/unparseable fields', () => {
  assert.deepStrictEqual(buildDict({}), {
    FirstDayOfWeek: 1,
    DateFormat: 0,
    AlarmVibePattern: 0,
    DefaultTuneId: 0, DefaultTunePreset: 0, DefaultVibeId: 0,
    DefaultSnoozeMinutes: 9,
    DefaultSnoozeMax: 3,
    DefaultSoundEnabled: 1,
    DefaultVibrationEnabled: 1,
    DefaultIncreasingVolume: 0,
    AudioVolume: 0,
  });
});

test('buildDict treats DefaultSnoozeMax "0" (unlimited) as a real value, not a default', () => {
  assert.deepStrictEqual(buildDict({ DefaultSnoozeMax: '0' }), {
    FirstDayOfWeek: 1,
    DateFormat: 0,
    AlarmVibePattern: 0,
    DefaultTuneId: 0, DefaultTunePreset: 0, DefaultVibeId: 0,
    DefaultSnoozeMinutes: 9,
    DefaultSnoozeMax: 0,
    DefaultSoundEnabled: 1,
    DefaultVibrationEnabled: 1,
    DefaultIncreasingVolume: 0,
    AudioVolume: 0,
  });
});

test('buildDict collapses DefaultSnoozeMinutes to 0 when DefaultSnoozeEnabled is false', () => {
  assert.deepStrictEqual(buildDict({ DefaultSnoozeMinutes: '15', DefaultSnoozeEnabled: false }), {
    FirstDayOfWeek: 1,
    DateFormat: 0,
    AlarmVibePattern: 0,
    DefaultTuneId: 0, DefaultTunePreset: 0, DefaultVibeId: 0,
    DefaultSnoozeMinutes: 0,
    DefaultSnoozeMax: 3,
    DefaultSoundEnabled: 1,
    DefaultVibrationEnabled: 1,
    DefaultIncreasingVolume: 0,
    AudioVolume: 0,
  });
});

test('buildDict sends DefaultSoundEnabled/DefaultVibrationEnabled as independent booleans', () => {
  assert.deepStrictEqual(buildDict({ DefaultSoundEnabled: false, DefaultVibrationEnabled: true }), {
    FirstDayOfWeek: 1,
    DateFormat: 0,
    AlarmVibePattern: 0,
    DefaultTuneId: 0, DefaultTunePreset: 0, DefaultVibeId: 0,
    DefaultSnoozeMinutes: 9,
    DefaultSnoozeMax: 3,
    DefaultSoundEnabled: 0,
    DefaultVibrationEnabled: 1,
    DefaultIncreasingVolume: 0,
    AudioVolume: 0,
  });
});

test('buildDict sends DefaultIncreasingVolume as an independent boolean', () => {
  assert.deepStrictEqual(buildDict({ DefaultIncreasingVolume: true }), {
    FirstDayOfWeek: 1,
    DateFormat: 0,
    AlarmVibePattern: 0,
    DefaultTuneId: 0, DefaultTunePreset: 0, DefaultVibeId: 0,
    DefaultSnoozeMinutes: 9,
    DefaultSnoozeMax: 3,
    DefaultSoundEnabled: 1,
    DefaultVibrationEnabled: 1,
    DefaultIncreasingVolume: 1,
    AudioVolume: 0,
  });
});

test('getDefaultClaySettings extracts every messageKey/defaultValue from config_clay.ts', () => {
  const defaults = getDefaultClaySettings(clayConfig);
  assert.deepStrictEqual(defaults, {
    AlarmDefaults: { snoozeMinutes: 10, snoozeMax: 3, vibration: true, sound: true, increasing: false, vibePattern: 0, vibeId: 0, tuneId: 0, soundPreset: 0 },
    FirstDayOfWeek: '1',
    DateFormat: '0',
    AudioVolume: 50,
    AlarmList: [],
    TuneList: [],
    VibeList: [],
  });
});

test('flattenAlarmDefaults produces the flat keys the watch protocol uses', () => {
  const flat = flattenAlarmDefaults({ AlarmDefaults: { snoozeMinutes: 7, snoozeMax: 0, vibration: false, sound: true, increasing: true, vibePattern: 2, vibeId: 0, tuneId: 5, soundPreset: 3 } });
  assert.strictEqual(flat.DefaultSnoozeMinutes, '7');
  assert.strictEqual(flat.DefaultSnoozeMax, '0');
  assert.strictEqual(flat.DefaultVibrationEnabled, false);
  assert.strictEqual(flat.DefaultSoundEnabled, true);
  assert.strictEqual(flat.DefaultIncreasingVolume, true);
  assert.strictEqual(flat.AlarmVibePattern, '2');
  assert.strictEqual(flat.DefaultTuneId, '5');
  assert.strictEqual(flat.DefaultTunePreset, '3');
  assert.strictEqual(flat.DefaultVibeId, '0');
  assert.deepStrictEqual(buildDict(flat), {
    FirstDayOfWeek: 1, DateFormat: 0, AlarmVibePattern: 2, DefaultTuneId: 5, DefaultTunePreset: 3, DefaultVibeId: 0, DefaultSnoozeMinutes: 7, DefaultSnoozeMax: 0,
    DefaultSoundEnabled: 1, DefaultVibrationEnabled: 0, DefaultIncreasingVolume: 1, AudioVolume: 0,
  });
  // snooze 0 = off: flat key says disabled, and buildDict sends minutes 0
  assert.strictEqual(flattenAlarmDefaults({ AlarmDefaults: { snoozeMinutes: 0 } }).DefaultSnoozeEnabled, false);
  // no AlarmDefaults: input passes through untouched
  assert.deepStrictEqual(flattenAlarmDefaults({ FirstDayOfWeek: '0' }), { FirstDayOfWeek: '0' });
});

test('migrateSavedSettings seeds AlarmDefaults from legacy flat keys, once', () => {
  const legacy = { DefaultSnoozeEnabled: true, DefaultSnoozeMinutes: '15', DefaultSnoozeMax: '5',
    DefaultSoundEnabled: false, DefaultVibrationEnabled: true, DefaultIncreasingVolume: true, AlarmVibePattern: '1', FirstDayOfWeek: '0' };
  const m = migrateSavedSettings(legacy);
  assert.deepStrictEqual(m.AlarmDefaults, { snoozeMinutes: 15, snoozeMax: 5, vibration: true, sound: false, increasing: true, vibePattern: 1, vibeId: 0, tuneId: 0, soundPreset: 0 });
  assert.strictEqual(m.FirstDayOfWeek, '0', 'other settings kept');
  // snooze disabled in the old toggle becomes minutes 0
  assert.strictEqual(migrateSavedSettings({ DefaultSnoozeEnabled: false, DefaultSnoozeMinutes: '10' }).AlarmDefaults.snoozeMinutes, 0);
  // wrapped values (Clay's { value }) are understood
  assert.strictEqual(migrateSavedSettings({ DefaultSnoozeMinutes: { value: '12' } }).AlarmDefaults.snoozeMinutes, 12);
  // already migrated, or nothing legacy: untouched
  const done = { AlarmDefaults: { snoozeMinutes: 3 }, DefaultSnoozeMinutes: '99' };
  assert.deepStrictEqual(migrateSavedSettings(done), done);
  assert.strictEqual(migrateSavedSettings({ FirstDayOfWeek: '1' }).AlarmDefaults, undefined);
});
