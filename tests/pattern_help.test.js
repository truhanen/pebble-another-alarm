// The config page shows the watch's built-in tunes/vibrations and its long
// pattern-syntax help read-only (src/ts/pattern_help.ts). The watch is the
// source of truth: this fails when that data drifts from src/c/main.c's preset
// tables or resources/text/pattern_help_*.txt.
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');
const H = require('../src/pkjs/pattern_help');
const L = require('../src/pkjs/list_config');

const root = path.join(__dirname, '..');
const mainC = fs.readFileSync(path.join(root, 'src/c/main.c'), 'utf8');

function presets(symbol) {
  const start = mainC.indexOf(symbol + '[] = {');
  assert.ok(start >= 0, symbol + ' not found in main.c');
  const body = mainC.slice(start, mainC.indexOf('};', start));
  const out = [];
  const re = /\{\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\}/g;
  let m;
  while ((m = re.exec(body))) { out.push({ name: m[1], pattern: m[2] }); }
  return out;
}

test('built-in tunes match SOUND_PRESETS on the watch, in order', () => {
  assert.deepStrictEqual(H.BUILTIN_TUNES, presets('SOUND_PRESETS'));
});

test('built-in vibrations match VIBE_PRESETS on the watch, in order', () => {
  assert.deepStrictEqual(H.BUILTIN_VIBES, presets('VIBE_PRESETS'));
});

test('every built-in pattern is valid under the phone validators', () => {
  H.BUILTIN_TUNES.forEach((b) => assert.ok(L.validTunePattern(b.pattern), b.name));
  H.BUILTIN_VIBES.forEach((b) => assert.ok(L.validVibePattern(b.pattern), b.name));
});

test('long help text is the watch\'s own help text', () => {
  const read = (f) => fs.readFileSync(path.join(root, 'resources/text', f), 'utf8').replace(/\n+$/, '');
  assert.strictEqual(H.TUNE_HELP, read('pattern_help_sound.txt'));
  assert.strictEqual(H.VIBE_HELP, read('pattern_help_vibe.txt'));
  assert.strictEqual(H.CRON_HELP, read('cron_help.txt'));
});
