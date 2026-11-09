// Key-namespace checks, so a collision fails a test instead of silently
// corrupting data (a vibration-library range once overlapped the vendored
// keyboard's settings keys, which made its "key wait time" read 150 ms).
//
//  1. Persist keys on the watch: every key (scalars and ranges, including the
//     keyboard's block in the app's own namespace) is unique and non-
//     overlapping, and every persist_*() call uses a known constant.
//  2. AppMessage keys (package.json messageKeys): unique names, ids unique and
//     = 10000 + declaration index, every key the C code or the phone JS uses is
//     declared, Clay-only keys never collide with wire keys, and the ids
//     documented in CLAUDE.md match the real ones.
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const read = (p) => fs.readFileSync(path.join(root, p), 'utf8');

// ---------------- persist keys ----------------

function persistKeys() {
  const store = read('src/c/alarm_store.h');
  const kbSettingsH = read('src/c/multitap_keyboard/multitap_keyboard/keyboard_settings.h');
  const kbSettingsC = read('src/c/multitap_keyboard/multitap_keyboard/keyboard_settings.c');
  const calc = read('src/c/alarm_calc.h');
  const num = (src, name) => { const m = new RegExp('#define\\s+' + name + '\\s+(\\d+)').exec(src); assert.ok(m, name + ' not found'); return parseInt(m[1], 10); };

  const keys = new Map();   // key number -> owner
  const add = (k, owner) => {
    assert.ok(!keys.has(k), `persist key ${k} used by both ${keys.get(k)} and ${owner}`);
    keys.set(k, owner);
  };

  // scalar keys: every "#define PERSIST_KEY_X <n>" that is not a *_BASE range
  const scalarRe = /#define\s+(PERSIST_KEY_[A-Z0-9_]+)\s+(\d+)/g;
  let m;
  while ((m = scalarRe.exec(store))) {
    if (/_BASE$/.test(m[1])) { continue; }
    add(parseInt(m[2], 10), m[1]);
  }
  // ranges: base .. base + count - 1
  const ranges = [
    ['PERSIST_KEY_ALARM_BASE', num(calc, 'MAX_ALARMS')],
    ['PERSIST_KEY_ALARM_EXT_BASE', num(calc, 'MAX_ALARMS')],
    ['PERSIST_KEY_TUNE_BASE', num(calc, 'MAX_CUSTOM_TUNES')],
    ['PERSIST_KEY_VIBE_BASE', num(calc, 'MAX_CUSTOM_VIBES')],
  ];
  for (const [name, count] of ranges) {
    const base = num(store, name);
    for (let i = 0; i < count; i++) { add(base + i, `${name}+${i}`); }
  }
  // the keyboard: MULTITAP_SETTINGS_BASE_KEY + offsets it really uses (see
  // keyboard_settings.c's PKEY_* defines); its documented block is 12 keys wide
  const kbBase = num(kbSettingsH, 'MULTITAP_SETTINGS_BASE_KEY');
  const offsets = [...kbSettingsC.matchAll(/#define\s+PKEY_[A-Z_]+\s+\(MULTITAP_SETTINGS_BASE_KEY \+ (\d+)\)/g)].map((x) => parseInt(x[1], 10));
  assert.ok(offsets.length >= 4, 'keyboard PKEY_ defines not found');
  assert.ok(Math.max(...offsets) < 12, 'keyboard uses a key past its documented 12-key block');
  for (let i = 0; i < 12; i++) { add(kbBase + i, `keyboard settings block +${i}`); }
  return { keys, kbBase };
}

test('persist keys never overlap (scalars, alarm/tune/vibration ranges, keyboard settings block)', () => {
  const { keys } = persistKeys();   // throws on any duplicate
  assert.ok(keys.size > 100);
});

test('the legacy vibration range is exactly the keyboard block it used to collide with', () => {
  // LEGACY_VIBE_BASE_KEY exists only to move old entries out of that block
  const { kbBase } = persistKeys();
  assert.strictEqual(parseInt(/#define\s+LEGACY_VIBE_BASE_KEY\s+(\d+)/.exec(read('src/c/alarm_store.h'))[1], 10), kbBase);
});

test('every persist_*() call on the watch uses a named key constant, never a literal', () => {
  const dirs = ['src/c', 'src/c/multitap_keyboard/multitap_keyboard'];
  const bad = [];
  for (const d of dirs) {
    for (const f of fs.readdirSync(path.join(root, d)).filter((x) => x.endsWith('.c'))) {
      read(path.join(d, f)).split('\n').forEach((line, i) => {
        const m = /persist_(?:read|write|exists|delete|get_size)\w*\(\s*([^,)]+)/.exec(line);
        if (m && /^\d+$/.test(m[1].trim())) { bad.push(`${d}/${f}:${i + 1}: ${line.trim()}`); }
      });
    }
  }
  assert.deepStrictEqual(bad, []);
});

// ---------------- AppMessage keys ----------------

const pkg = JSON.parse(read('package.json'));
const messageKeys = pkg.pebble.messageKeys;

test('messageKeys: names unique, none looks like an array-syntax key', () => {
  assert.strictEqual(new Set(messageKeys).size, messageKeys.length, 'duplicate key name in package.json');
  // an array key ("Name[N]") claims N ids up front and would shift every other id
  assert.ok(messageKeys.every((k) => !/\[\d+\]$/.test(k)), 'array-syntax key present: ids are no longer 10000+index');
});

test('generated ids are unique and equal 10000 + declaration index (needs a prior `pebble build`)', (t) => {
  const f = path.join(root, 'build/src/message_keys.auto.c');
  if (!fs.existsSync(f)) { t.skip('build/src/message_keys.auto.c missing: run `pebble build` first'); return; }
  const ids = {};
  for (const m of fs.readFileSync(f, 'utf8').matchAll(/uint32_t MESSAGE_KEY_(\w+)\s*=\s*(\d+);/g)) { ids[m[1]] = parseInt(m[2], 10); }
  assert.deepStrictEqual(Object.keys(ids).sort(), [...messageKeys].sort(), 'generated keys differ from package.json');
  assert.strictEqual(new Set(Object.values(ids)).size, Object.keys(ids).length, 'two keys share an id');
  messageKeys.forEach((k, i) => assert.strictEqual(ids[k], 10000 + i, `${k} should be ${10000 + i}`));
});

test('every MESSAGE_KEY_* the watch code uses is declared', () => {
  const used = new Set([...read('src/c/main.c').matchAll(/MESSAGE_KEY_(\w+)/g)].map((m) => m[1]));
  const missing = [...used].filter((k) => !messageKeys.includes(k));
  assert.deepStrictEqual(missing, []);
});

test('every key the phone sends to the watch is declared, and Clay-only keys never collide with wire keys', () => {
  const sent = new Set();
  // buildDict's result keys (dict.ts) and the list/snapshot keys (index.ts)
  const dictSrc = read('src/ts/dict.ts');
  const body = dictSrc.slice(dictSrc.indexOf('export function buildDict'));
  for (const m of body.matchAll(/^\s{4}(\w+):/gm)) { sent.add(m[1]); }
  for (const m of read('src/ts/index.ts').matchAll(/sendAppMessage\(\s*\{\s*(\w+):/g)) { sent.add(m[1]); }
  assert.ok(sent.has('FirstDayOfWeek') && sent.has('VibeConfig'), 'key extraction found nothing: ' + [...sent]);
  assert.deepStrictEqual([...sent].filter((k) => !messageKeys.includes(k)), []);

  const clayOnly = ['AlarmList', 'TuneList', 'VibeList', 'AlarmDefaults', 'DefaultSnoozeEnabled'];
  assert.deepStrictEqual(clayOnly.filter((k) => messageKeys.includes(k)), [], 'a Clay-only setting shares a name with a wire key');
});

test('message key ids documented in CLAUDE.md match the real ones', () => {
  const rows = [...read('CLAUDE.md').matchAll(/^\|\s*`(\w+)`\s*\|\s*(\d{5})\s*\|/gm)];
  assert.ok(rows.length > 20, 'documented key table not found');
  const wrong = rows.filter(([, name, id]) => {
    const i = messageKeys.indexOf(name);
    return i >= 0 && 10000 + i !== parseInt(id, 10);
  }).map(([, name, id]) => `${name}: documented ${id}, actual ${10000 + messageKeys.indexOf(name)}`);
  assert.deepStrictEqual(wrong, []);
});
