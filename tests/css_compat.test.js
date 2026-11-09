// The config page runs inside the phone app's webview, which can be older than
// the browser the tests use. Keep the component CSS to features that webviews
// from before ~2020 still handle; the `inset` shorthand (Chrome 87 / Safari
// 14.1) made the on/off switch and the weekday/option chips lose their whole
// tap area on a webview without it, so use explicit top/right/bottom/left.
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const dir = path.join(__dirname, '..', 'src', 'ts');
const files = fs.readdirSync(dir).filter((f) => /^config_.*\.ts$/.test(f));

test('component CSS avoids newer shorthands and selectors', () => {
  const banned = [
    [/[;{'"\s]inset\s*:/, 'the inset shorthand (use top/right/bottom/left)'],
    [/aspect-ratio\s*:/, 'aspect-ratio'],
    [/:has\(/, ':has()'],
    [/@container/, '@container queries'],
    [/\bcolor-mix\(/, 'color-mix()'],
  ];
  const found = [];
  for (const f of files) {
    fs.readFileSync(path.join(dir, f), 'utf8').split('\n').forEach((line, i) => {
      for (const [re, what] of banned) {
        if (re.test(line) && !/^\s*(\/\/|\*)/.test(line)) { found.push(`${f}:${i + 1}: ${what}`); }
      }
    });
  }
  assert.deepStrictEqual(found, []);
});
