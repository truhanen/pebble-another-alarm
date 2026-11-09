const test = require('node:test');
const assert = require('node:assert');
const L = require('../src/pkjs/list_config');

test('validCronField mirrors ac_cron_parse_field', () => {
  for (const ok of ['*', '5', '1-5', '*/20', '9-17/2', '1,5,10-15', '5/2']) {
    assert.ok(L.validCronField(ok, 0, 59), ok);
  }
  for (const bad of ['', '60', '5-1', '*/0', 'a', '1,', ',1', '1--2', '*/']) {
    assert.ok(!L.validCronField(bad, 0, 59), bad);
  }
  assert.ok(!L.validCronField('0', 1, 31));
});

test('normalizeCron pads and validates', () => {
  assert.strictEqual(L.normalizeCron('*/20 9-17'), '*/20 9-17 * * * *');
  assert.strictEqual(L.normalizeCron('0 7 * * 1-5 */2'), '0 7 * * 1-5 */2');
  assert.strictEqual(L.normalizeCron('0 7 * * 1-5 * *'), null);
  assert.strictEqual(L.normalizeCron('0 25'), null);
  assert.strictEqual(L.normalizeCron(''), null);
});

test('validTunePattern', () => {
  for (const ok of ['C z E z G', '^C/2 z C3 _D', 'C,,, C\'', 'A2/3 B', 'z/2']) {
    assert.ok(L.validTunePattern(ok), ok);
  }
  for (const bad of ['', 'H', 'C0', 'C9', "C',", 'C,,,,,,,,,', '2', 'C/', '^^C']) {
    assert.ok(!L.validTunePattern(bad), bad);
  }
  assert.ok(!L.validTunePattern(new Array(33).fill('C').join(' ')));
  assert.ok(L.validTunePattern(new Array(32).fill('C').join(' ')));
});

test('tunesToString drops invalid/unnamed and keeps ids', () => {
  const s = L.tunesToString([
    { id: 70000, name: 'Good', pattern: 'C z E' },
    { id: 70001, name: '', pattern: 'C' },
    { id: 70002, name: 'Bad', pattern: 'Q' },
  ]);
  assert.strictEqual(s, '70000\x1fGood\x1fC z E');
  assert.strictEqual(L.tunesToString([]), '');
});

test('alarmsToString basic + cron, tune id validation, separators scrubbed', () => {
  const base = { id: 70000, name: 'A\x1fB', enabled: true, isCron: false, hour: 7, minute: 5,
    repeatDays: 62, cron: '', snoozeMinutes: 10, snoozeMax: 3, vibration: true, sound: false,
    tuneId: 123, autoDismiss: 1, increasingVolume: true };
  const cron = Object.assign({}, base, { id: 70001, isCron: true, cron: '*/20 9-17' });
  const badCron = Object.assign({}, base, { id: 70002, isCron: true, cron: 'nope' });
  const s = L.alarmsToString([base, cron, badCron], [123]);
  const recs = s.split('\x1e');
  assert.strictEqual(recs.length, 2);
  assert.strictEqual(recs[0], ['70000', 'A B', '1', '0', '7', '5', '62', '*', '10', '3', '1', '0', '123', '1', '1'].join('\x1f'));
  assert.strictEqual(recs[1].split('\x1f')[3], '1');
  assert.strictEqual(recs[1].split('\x1f')[7], '*/20 9-17 * * * *');
  assert.strictEqual(L.alarmsToString([base], []).split('\x1f')[12], '0');
});

test('genId is phone-owned range', () => {
  for (let i = 0; i < 50; i++) { assert.ok(L.genId() >= L.PHONE_ID_MIN); }
});
