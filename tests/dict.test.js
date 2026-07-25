const test = require('node:test');
const assert = require('node:assert');
const { buildDict } = require('../src/pkjs/dict');

// Arbitrary fixed base id for DefaultAlarmSignal[2], standing in for
// whatever `message_keys` module resolves it to at build time — only its
// relative offset (base, base+1) matters to buildDict.
const KEYS = { DefaultAlarmSignal: 10000 };

test('buildDict parses Clay string values to ints', () => {
  assert.deepStrictEqual(buildDict({
    FirstDayOfWeek: '0',
    AlarmVibePattern: '2',
    DefaultSnoozeMinutes: '15',
    DefaultSnoozeMax: '5',
    AudioVolume: '40',
  }, KEYS), {
    FirstDayOfWeek: 0,
    AlarmVibePattern: 2,
    DefaultSnoozeMinutes: 15,
    DefaultSnoozeMax: 5,
    AudioVolume: 40,
    10000: 1,
    10001: 1,
  });
});

test('buildDict accepts AudioVolume as a number (Clay slider type)', () => {
  assert.deepStrictEqual(buildDict({ AudioVolume: 75 }, KEYS), {
    FirstDayOfWeek: 1,
    AlarmVibePattern: 0,
    DefaultSnoozeMinutes: 9,
    DefaultSnoozeMax: 3,
    AudioVolume: 75,
    10000: 1,
    10001: 1,
  });
});

test('buildDict defaults missing/unparseable fields', () => {
  assert.deepStrictEqual(buildDict({}, KEYS), {
    FirstDayOfWeek: 1,
    AlarmVibePattern: 0,
    DefaultSnoozeMinutes: 9,
    DefaultSnoozeMax: 3,
    AudioVolume: 0,
    10000: 1,
    10001: 1,
  });
});

test('buildDict treats DefaultSnoozeMax "0" (unlimited) as a real value, not a default', () => {
  assert.deepStrictEqual(buildDict({ DefaultSnoozeMax: '0' }, KEYS), {
    FirstDayOfWeek: 1,
    AlarmVibePattern: 0,
    DefaultSnoozeMinutes: 9,
    DefaultSnoozeMax: 0,
    AudioVolume: 0,
    10000: 1,
    10001: 1,
  });
});

test('buildDict sends DefaultAlarmSignal as two plain scalar ints at base/base+1, not one array-valued key', () => {
  assert.deepStrictEqual(buildDict({ DefaultAlarmSignal: [false, true] }, KEYS), {
    FirstDayOfWeek: 1,
    AlarmVibePattern: 0,
    DefaultSnoozeMinutes: 9,
    DefaultSnoozeMax: 3,
    AudioVolume: 0,
    10000: 0,
    10001: 1,
  });
});

test('buildDict computes the second DefaultAlarmSignal key from whatever base id it is given', () => {
  const result = buildDict({ DefaultAlarmSignal: [true, false] }, { DefaultAlarmSignal: 42 });
  assert.strictEqual(result[42], 1);
  assert.strictEqual(result[43], 0);
});
