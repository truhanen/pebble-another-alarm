// Browser-level tests for the phone config page (accordion + Alarms/Tunes
// lists). Drives the real Clay page in headless Chromium via Playwright.
// Run: pebble build && npm run test:config
const test = require('node:test');
const assert = require('node:assert');
const { chromium } = require('playwright');
const { loadPhone, openConfigPage, CLOSE_URL } = require('./harness');

let browser;
test.before(async () => { browser = await chromium.launch(); });
test.after(async () => { await browser.close(); });

// Opens a fresh page; returns { page, phone, save() } where save() clicks
// Save, feeds the response to webviewclosed, and returns the sent dicts.
async function open(initialStorage) {
  const phone = loadPhone(initialStorage);
  const html = openConfigPage(phone);
  const page = await browser.newPage({ viewport: { width: 390, height: 844 } });
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  // Save navigates to CLOSE_URL + <encoded settings>. Answer the request so
  // the navigation completes, then read the data back from the frame URL
  // (route matching ignores the #fragment where the data lives).
  await page.route('http://harness.invalid/**', (route) => route.fulfill({ status: 200, contentType: 'text/html', body: '<html></html>' }));
  await page.setContent(html);
  await page.waitForSelector('.component-heading');
  return {
    page, phone, errors,
    async save() {
      await page.click('.component-submit button');
      await page.waitForURL((u) => u.href.startsWith(CLOSE_URL), { timeout: 5000 });
      const closeUrl = page.url();
      phone.sent.length = 0;
      phone.fire('webviewclosed', { response: closeUrl.slice(CLOSE_URL.length) });
      return phone.sent;
    },
  };
}

const heading = (page, label) => page.locator('.component-heading', { hasText: label });
const visible = (page, sel) => page.locator(sel).first().isVisible();

test('accordion: all sections closed on load, one open at a time', async () => {
  const { page, errors } = await open();
  for (const name of ['Configuration', 'Alarms', 'Tunes']) {
    assert.ok(await heading(page, name).isVisible(), name + ' heading visible');
  }
  assert.ok(!(await visible(page, '.al-add')), 'Alarms closed');
  assert.ok(!(await visible(page, '.tu-add')), 'Tunes closed');
  assert.ok(!(await page.getByText('Alarm volume').first().isVisible()), 'Configuration closed');
  assert.ok((await heading(page, 'Alarms').innerText()).includes('▸'), 'collapsed chevron');

  await heading(page, 'Alarms').click();
  assert.ok(await visible(page, '.al-add'), 'Alarms open');
  assert.ok((await heading(page, 'Alarms').innerText()).includes('▾'), 'open chevron');

  await heading(page, 'Tunes').click();
  assert.ok(await visible(page, '.tu-add'), 'Tunes open');
  assert.ok(!(await visible(page, '.al-add')), 'Alarms auto-closed');

  await heading(page, 'Tunes').click();
  assert.ok(!(await visible(page, '.tu-add')), 'click on open heading closes it');
  assert.deepStrictEqual(errors, []);
});

test('Configuration section holds the global settings; Save bar always visible', async () => {
  const { page } = await open();
  await heading(page, 'Configuration').click();
  assert.ok(await page.getByText('First day of week').first().isVisible());
  assert.ok(await page.getByText('Date format').first().isVisible());
  assert.ok(await page.locator('.component-submit').isVisible(), 'Save bar visible while a section is open');
  await heading(page, 'Configuration').click();
  assert.ok(await page.locator('.component-submit').isVisible(), 'Save bar visible with all closed');
});

test('alarms: add, Basic/Cron toggle, validation tag, delete', async () => {
  const { page } = await open();
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  assert.strictEqual(await page.locator('.al-row').count(), 1);
  assert.ok(await visible(page, '.al-time'), 'Basic shows time');
  assert.ok(!(await visible(page, '.al-cronin')), 'Basic hides cron field');

  await page.selectOption('.al-mode', '1');
  assert.ok(!(await visible(page, '.al-time')), 'Cron hides time');
  assert.ok(await visible(page, '.al-cronin'), 'Cron shows cron field');
  assert.ok(!(await visible(page, '.al-d')), 'Cron hides weekday boxes');

  await page.fill('.al-cronin', '*/20 99');
  assert.ok((await page.locator('.al-err').innerText()).includes('Invalid'), 'invalid tag shown');
  await page.fill('.al-cronin', '*/20 9-17 * * 1-5');
  assert.strictEqual(await page.locator('.al-err').innerText(), '', 'tag cleared when valid');

  await page.click('.al-add');
  assert.strictEqual(await page.locator('.al-row').count(), 2);
  await page.locator('.al-del').first().click();
  assert.strictEqual(await page.locator('.al-row').count(), 1);
});

test('tunes: validation tag and delete', async () => {
  const { page } = await open();
  await heading(page, 'Tunes').click();
  await page.click('.tu-add');
  await page.fill('.tu-pat', 'C Q');
  assert.ok((await page.locator('.tu-err').innerText()).includes('Invalid'));
  await page.fill('.tu-pat', "C z E2 G,'");
  assert.ok((await page.locator('.tu-err').innerText()).includes('Invalid'), "mixed octave markers are invalid");
  await page.fill('.tu-pat', 'C z E2 ^G/2');
  assert.strictEqual(await page.locator('.tu-err').innerText(), '');
  await page.locator('.tu-del').click();
  assert.strictEqual(await page.locator('.tu-row').count(), 0);
});

test('max rows hides the Add button (16 alarms, 20 tunes)', async () => {
  const { page } = await open();
  await heading(page, 'Alarms').click();
  for (let i = 0; i < 16; i++) { await page.click('.al-add'); }
  assert.strictEqual(await page.locator('.al-row').count(), 16);
  assert.ok(!(await visible(page, '.al-add')), 'Add hidden at 16');
  await heading(page, 'Tunes').click();
  for (let i = 0; i < 20; i++) { await page.click('.tu-add'); }
  assert.ok(!(await visible(page, '.tu-add')), 'Add hidden at 20');
});

test('tune dropdown in an alarm follows the Tunes list', async () => {
  const { page } = await open();
  await heading(page, 'Tunes').click();
  await page.click('.tu-add');
  await page.fill('.tu-name', 'Mine');
  await page.fill('.tu-pat', 'C E G');
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  await page.locator('.al-tune').dispatchEvent('mousedown');
  const labels = await page.locator('.al-tune option').allInnerTexts();
  assert.deepStrictEqual(labels, ['Default tune', 'Mine']);

  // An invalid pattern (or no name) must not be offered: Save would drop it.
  await heading(page, 'Tunes').click();
  await page.click('.tu-add');
  await page.locator('.tu-name').nth(1).fill('Broken');
  await page.locator('.tu-pat').nth(1).fill('C Q');
  await page.click('.tu-add');
  await page.locator('.tu-pat').nth(2).fill('C E');   // valid but unnamed
  await heading(page, 'Alarms').click();
  await page.locator('.al-tune').dispatchEvent('mousedown');
  assert.deepStrictEqual(await page.locator('.al-tune option').allInnerTexts(), ['Default tune', 'Mine']);

  // Fixing the pattern makes it appear (live, via the TuneList change event).
  await heading(page, 'Tunes').click();
  await page.locator('.tu-pat').nth(1).fill('C E G');
  await page.locator('.tu-pat').nth(1).dispatchEvent('change');
  await heading(page, 'Alarms').click();
  await page.locator('.al-tune').dispatchEvent('mousedown');
  assert.deepStrictEqual(await page.locator('.al-tune option').allInnerTexts(), ['Default tune', 'Mine', 'Broken']);
});

test('Save sends tunes then alarms, with validation, padding and tune ids', async () => {
  const t = await open();
  const { page } = t;
  await heading(page, 'Tunes').click();
  await page.click('.tu-add');
  await page.locator('.tu-name').nth(0).fill('Mine'); await page.locator('.tu-pat').nth(0).fill('C z E');
  await page.click('.tu-add');
  await page.locator('.tu-name').nth(1).fill('Broken'); await page.locator('.tu-pat').nth(1).fill('Q');
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  await page.locator('.al-name').nth(0).fill('Weekday');
  await page.fill('.al-time', '06:30');
  await page.locator('.al-d[data-d="1"]').check();
  await page.locator('.al-d[data-d="5"]').check();
  await page.locator('.al-tune').dispatchEvent('mousedown');
  await page.selectOption('.al-tune', { label: 'Mine' });
  await page.click('.al-add');
  await page.locator('.al-name').nth(1).fill('Cron');
  await page.locator('.al-mode').nth(1).selectOption('1');
  await page.locator('.al-cronin').nth(1).fill('*/20 9-17');
  await page.click('.al-add');   // third alarm: invalid cron, must be dropped
  await page.locator('.al-mode').nth(2).selectOption('1');
  await page.locator('.al-cronin').nth(2).fill('bogus');

  const sent = await t.save();
  const keys = sent.map((d) => Object.keys(d)[0]);
  assert.deepStrictEqual(keys, ['FirstDayOfWeek', 'TuneConfig', 'AlarmConfig']);
  const tunes = sent[1].TuneConfig.split('\x1e');
  assert.strictEqual(tunes.length, 1, 'invalid tune dropped');
  const [tid, tname, tpat] = tunes[0].split('\x1f');
  assert.strictEqual(tname, 'Mine'); assert.strictEqual(tpat, 'C z E');
  assert.ok(parseInt(tid, 10) >= 0x10000);
  const alarms = sent[2].AlarmConfig.split('\x1e').map((r) => r.split('\x1f'));
  assert.strictEqual(alarms.length, 2, 'invalid-cron alarm dropped');
  assert.deepStrictEqual(alarms[0].slice(1, 8), ['Weekday', '1', '0', '6', '30', String((1 << 1) | (1 << 5)), '*']);
  assert.strictEqual(alarms[0][12], tid, 'alarm references the tune id');
  assert.deepStrictEqual(alarms[1].slice(1, 4), ['Cron', '1', '1']);
  assert.strictEqual(alarms[1][7], '*/20 9-17 * * * *');
  assert.strictEqual(t.phone.store.lists_pending, undefined, 'pending flag cleared after delivery');
});

test('saved lists reload into the page', async () => {
  const t = await open();
  await heading(t.page, 'Tunes').click();
  await t.page.click('.tu-add'); await t.page.fill('.tu-name', 'Mine'); await t.page.fill('.tu-pat', 'C E G');
  await heading(t.page, 'Alarms').click();
  await t.page.click('.al-add'); await t.page.locator('.al-name').fill('Persisted');
  await t.save();
  const page2 = await open({ 'clay-settings': t.phone.store['clay-settings'] });
  await heading(page2.page, 'Alarms').click();
  assert.strictEqual(await page2.page.locator('.al-name').inputValue(), 'Persisted');
  await heading(page2.page, 'Tunes').click();
  assert.strictEqual(await page2.page.locator('.tu-name').inputValue(), 'Mine');
  assert.deepStrictEqual(page2.errors, []);
});

test('failed delivery keeps lists pending; next launch resends them', async () => {
  const t = await open();
  await heading(t.page, 'Alarms').click();
  await t.page.click('.al-add'); await t.page.locator('.al-name').fill('A');
  t.phone.failSends = true;
  await t.save();
  assert.strictEqual(t.phone.store.lists_pending, '1');
  // Next phone-side launch ('ready'): watch is reachable now.
  t.phone.failSends = false;
  t.phone.sent.length = 0;
  t.phone.fire('ready');
  const keys = t.phone.sent.map((d) => Object.keys(d)[0]);
  assert.ok(keys.includes('TuneConfig') && keys.includes('AlarmConfig'), 'lists resent: ' + keys);
  assert.strictEqual(t.phone.store.lists_pending, undefined);
});

test('plain launch does not resend lists (would revert watch-side edits)', async () => {
  const t = await open();
  await heading(t.page, 'Alarms').click();
  await t.page.click('.al-add');
  await t.save();   // delivered, flag cleared
  t.phone.sent.length = 0;
  t.phone.fire('ready');
  const keys = t.phone.sent.map((d) => Object.keys(d)[0]);
  assert.ok(!keys.includes('AlarmConfig') && !keys.includes('TuneConfig'), 'sent: ' + keys);
});
