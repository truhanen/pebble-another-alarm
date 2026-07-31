const test = require('node:test');
const assert = require('node:assert');
const { buildDict } = require('../src/pkjs/dict');

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
    DefaultSnoozeMinutes: 9,
    DefaultSnoozeMax: 3,
    DefaultSoundEnabled: 1,
    DefaultVibrationEnabled: 1,
    DefaultIncreasingVolume: 1,
    AudioVolume: 0,
  });
});

test('buildDict parses DateFormat', () => {
  assert.deepStrictEqual(buildDict({ DateFormat: '1' }), {
    FirstDayOfWeek: 1,
    DateFormat: 1,
    AlarmVibePattern: 0,
    DefaultSnoozeMinutes: 9,
    DefaultSnoozeMax: 3,
    DefaultSoundEnabled: 1,
    DefaultVibrationEnabled: 1,
    DefaultIncreasingVolume: 0,
    AudioVolume: 0,
  });
});
