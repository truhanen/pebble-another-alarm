// The phone validates tune and vibration patterns in TypeScript; the watch
// parses them with alarm_calc.c. This builds the real C parser
// (tests/pattern_parity.c) and checks both agree on a hand-written plus a
// randomly generated corpus, so the duplicated grammar can't silently drift.
const test = require('node:test');
const assert = require('node:assert');
const { execFileSync, spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const L = require('../src/pkjs/list_config');

const root = path.join(__dirname, '..');
const hasGcc = spawnSync('gcc', ['--version']).status === 0;

// Deterministic pseudo-random generator so failures are reproducible.
let seed = 12345;
const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
const pick = (a) => a[Math.floor(rnd() * a.length)];

const SOUND_TOKENS = ['C', 'D', 'E', 'F', 'G', 'A', 'B', '^C', '_D', '^^C', 'C,', "C'", 'C,,,', 'C,,,,,,,,', 'C,,,,,,,,,',
  "C',", 'A,,2', 'z', 'z2', 'z/2', 'z0', '1', '2', '8', '9', '0', '/2', '3/4', 'C2', 'C/3', 'C9', 'C1/9', 'H', 'x', "C'3", 'C3,', '^z'];
const VIBE_TOKENS = ['1', '2', '8', '9', '0', '/2', '/8', '/9', '1/2', '3/4', '8/8', '2/2', '1/', '1//2', 'z', 'z2', 'z/2', 'z3/4', 'z9', 'C', 'x', '^1', '12', '1.5'];

function corpus(tokens, maxTokens) {
  const out = ['', ' ', 'C', '1', 'z', ' C  z ', 'C\tz'];
  for (let i = 0; i < 600; i++) {
    const n = 1 + Math.floor(rnd() * (maxTokens + 2));
    let s = '';
    for (let j = 0; j < n; j++) { s += (j && rnd() > 0.04 ? ' ' : '') + pick(tokens); }
    out.push(s);
  }
  // the exact segment-cap boundaries
  out.push(new Array(8).fill('1').join(' '), new Array(9).fill('1').join(' '));
  out.push(new Array(32).fill('C').join(' '), new Array(33).fill('C').join(' '));
  // the character-length boundary the phone adds on top of the watch's parser
  const base = new Array(22).fill("^C'7/8").join(' ');   // 153 characters
  out.push(base + " ^C'7/8", base + ' ^C,,7/8');          // 160 and 161 characters, both fine for the parser
  return out;
}

test('phone validators agree with the watch parser', { skip: !hasGcc && 'gcc not available' }, () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'parity-'));
  const bin = path.join(dir, 'parity');
  execFileSync('gcc', ['-I', path.join(root, 'src/c'), path.join(root, 'tests/pattern_parity.c'),
    path.join(root, 'src/c/alarm_calc.c'), '-o', bin]);

  const check = (kind, texts, validator) => {
    const input = texts.map((t) => `${kind}|${t}`).join('\n') + '\n';
    const res = execFileSync(bin, { input }).toString().trim().split('\n');
    assert.strictEqual(res.length, texts.length);
    const bad = [];
    // The parser has no character limit; tunes also have the 160-character cap
    // the phone enforces (and the watch's own keyboard, and its push handler).
    const expected = (t, watchOk) => watchOk && (kind !== 'S' || t.replace(/^ +| +$/g, '').length <= L.PATTERN_MAX_CHARS);
    texts.forEach((t, i) => { if (expected(t, res[i] === '1') !== validator(t)) { bad.push(JSON.stringify(t) + ' watch=' + res[i]); } });
    assert.deepStrictEqual(bad, [], `${kind}: validators disagree with the watch on ${bad.length} patterns`);
  };
  check('S', corpus(SOUND_TOKENS, 6), L.validTunePattern);
  check('V', corpus(VIBE_TOKENS, 6), L.validVibePattern);
});

module.exports = { corpus, SOUND_TOKENS, VIBE_TOKENS };

test('the 160-character tune cap sits exactly where documented', () => {
  const base = new Array(22).fill("^C'7/8").join(' ');
  assert.strictEqual((base + " ^C'7/8").length, 160);
  assert.ok(L.validTunePattern(base + " ^C'7/8"), '160 characters is allowed');
  assert.ok(!L.validTunePattern(base + ' ^C,,7/8'), '161 characters is not');
  assert.ok(L.validTunePattern(' ' + base + " ^C'7/8 "), 'surrounding spaces are not counted');
});
