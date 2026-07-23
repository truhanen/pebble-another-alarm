const test = require('node:test');
const assert = require('node:assert');
const { buildDict } = require('../src/pkjs/dict');

test('buildDict converts booleans to 0/1', () => {
  assert.deepStrictEqual(buildDict({ ExampleSetting: true }), { ExampleSetting: 1 });
  assert.deepStrictEqual(buildDict({ ExampleSetting: false }), { ExampleSetting: 0 });
});

test('buildDict defaults a missing setting to 0', () => {
  assert.deepStrictEqual(buildDict({}), { ExampleSetting: 0 });
});
