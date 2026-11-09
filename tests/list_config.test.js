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
  assert.strictEqual(recs[0], ['70000', 'A B', '1', '0', '7', '5', '62', '*', '10', '3', '1', '0', '123', '1', '1', '0', '0'].join('\x1f'));
  assert.strictEqual(recs[1].split('\x1f')[3], '1');
  assert.strictEqual(recs[1].split('\x1f')[7], '*/20 9-17 * * * *');
  assert.strictEqual(L.alarmsToString([base], []).split('\x1f')[12], '0');
});

test('genId is phone-owned range', () => {
  for (let i = 0; i < 50; i++) { assert.ok(L.genId() >= L.PHONE_ID_MIN); }
});

test('tombstones: bare-id records, only for ids no longer present', () => {
  const a = { id: 70000, name: 'A', enabled: true, isCron: false, hour: 7, minute: 0, repeatDays: 0, cron: '',
    snoozeMinutes: 10, snoozeMax: 3, vibration: true, sound: true, tuneId: 0, autoDismiss: 0, increasingVolume: false };
  const recs = L.alarmsToString([a], [], [70000, 5, 6, 5]).split('\x1e');
  assert.strictEqual(recs.length, 3);
  assert.deepStrictEqual(recs.slice(1), ['5', '6']);
  assert.strictEqual(L.alarmsToString([], [], [7]), '7');
  assert.strictEqual(L.tunesToString([], [3, 3]), '3');
});

test('parseAlarms/parseTunes round-trip what the phone sends', () => {
  const base = { id: 3, name: 'W', enabled: false, isCron: false, hour: 6, minute: 30, repeatDays: 62, cron: '',
    snoozeMinutes: 5, snoozeMax: 0, vibration: false, sound: true, tuneId: 4, autoDismiss: 2, increasingVolume: true,
    vibeId: 9, vibePreset: 2 };
  const cron = Object.assign({}, base, { id: 8, isCron: true, cron: '*/20 9-17' });
  const str = L.alarmsToString([base, cron], [4], [], [9]);
  const back = L.parseAlarms(str);
  assert.strictEqual(back.length, 2);
  // A basic alarm has no cron text of its own; the page seeds a default.
  assert.deepStrictEqual(back[0], Object.assign({}, base, { cron: '0 7 * * *' }));
  assert.strictEqual(back[1].isCron, true);
  assert.strictEqual(back[1].cron, '*/20 9-17 * * * *');
  const tunes = [{ id: 4, name: 'T', pattern: "^C,, z/2 E'3" }];
  assert.deepStrictEqual(L.parseTunes(L.tunesToString(tunes)), tunes);
  const vibes = [{ id: 9, name: 'V', pattern: '2 z/2 1/2' }];
  assert.deepStrictEqual(L.parseVibes(L.vibesToString(vibes)), vibes);
  assert.strictEqual(L.vibesToString([{ id: 1, name: 'x', pattern: 'C' }]), '', 'invalid vibration pattern dropped');
  assert.strictEqual(L.vibesToString([], [5]), '5', 'tombstone');
  assert.deepStrictEqual(L.parseAlarms(''), []);
  assert.deepStrictEqual(L.parseAlarms('7'), [], 'tombstone-only records are not alarms');
});

test('cronNext finds the next matching minute', () => {
  // Monday 2026-11-09 10:05 local
  const now = new Date(2026, 10, 9, 10, 5, 0);
  const fmt = (d) => d && `${d.getFullYear()}-${d.getMonth() + 1}-${d.getDate()} ${d.getHours()}:${String(d.getMinutes()).padStart(2, '0')}`;
  assert.strictEqual(fmt(L.cronNext('*/20 9-17 * * 1-5', now)), '2026-11-9 10:20');
  assert.strictEqual(fmt(L.cronNext('0 7 * * *', now)), '2026-11-10 7:00');
  assert.strictEqual(fmt(L.cronNext('5 10 * * *', now)), '2026-11-10 10:05', 'strictly after now');
  assert.strictEqual(fmt(L.cronNext('0 8 * * 0', now)), '2026-11-15 8:00', 'Sunday');
  assert.strictEqual(fmt(L.cronNext('0 12 1 * *', now)), '2026-12-1 12:00', 'day of month');
  assert.strictEqual(fmt(L.cronNext('0 0 * 2 *', now)), '2027-2-1 0:00', 'month');
  assert.strictEqual(fmt(L.cronNext('0 9 * * 1 */2', now)), '2026-11-16 9:00', 'biweekly skips this even ISO week');
  assert.strictEqual(L.cronNext('0 0 30 2 *', now), null, 'Feb 30 never matches');
  assert.strictEqual(L.cronNext('nope', now), null);
});

test('names are cut at 31 UTF-8 bytes on a character boundary (the watch stores bytes)', () => {
  assert.strictEqual(L.sanitizeName('x'.repeat(40)).length, 31);
  const accented = L.sanitizeName('\u00e9'.repeat(40));            // 2 bytes each
  assert.strictEqual(Buffer.byteLength(accented), 30);
  assert.strictEqual(accented.length, 15);
  const emoji = L.sanitizeName('\u{1F600}'.repeat(20));            // 4 bytes each
  assert.strictEqual(Buffer.byteLength(emoji), 28);
  assert.ok(!/[\ud800-\udbff]$/.test(emoji), 'never ends on half a surrogate pair');
  assert.strictEqual(L.sanitizeName('a\u0001b'), 'a b', 'control characters are replaced');
  assert.strictEqual(L.sanitizeName(undefined), '');
});

test('a pattern over 160 characters is dropped from a tunes push, 160 is kept', () => {
  const base = new Array(22).fill("^C'7/8").join(' ');
  const ok = base + " ^C'7/8";
  const rec = (id, pat) => ({ id: id, name: 'T', pattern: pat });
  const s = L.tunesToString([rec(70000, ok), rec(70001, base + ' ^C,,7/8')]);
  assert.strictEqual(s.split('\x1e').length, 1);
  assert.ok(s.startsWith('70000'));
});
