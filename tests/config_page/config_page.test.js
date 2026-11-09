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
async function open(initialStorage, beforeOpen) {
  const phone = loadPhone(initialStorage);
  if (beforeOpen) { beforeOpen(phone); }
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
  // Order matters: defaults first, Vibration patterns right after Tunes, Other last.
  const names = ['New alarm defaults', 'Alarms', 'Tunes', 'Vibration patterns', 'Other'];
  const shown = (await page.locator('.component-heading').allInnerTexts()).map((t) => t.replace(/^\S+\s/, '').toLowerCase());
  assert.deepStrictEqual(shown, names.map((n) => n.toLowerCase()));
  assert.ok(!(await visible(page, '.al-add')), 'Alarms closed');
  assert.ok(!(await visible(page, '.tu-add')), 'Tunes closed');
  assert.ok(!(await page.getByText('Alarm volume').first().isVisible()), 'Other closed');
  assert.ok(!(await visible(page, '.ad-sm')), 'New alarm defaults closed');
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

test('settings are split between "New alarm defaults" and "Other", without sub-header rows', async () => {
  const { page } = await open();
  await heading(page, 'New alarm defaults').click();
  assert.ok(await visible(page, '.ad-sm'));
  assert.ok(await page.getByText('Defaults for newly created alarms').first().isVisible());
  assert.ok(!(await page.getByText('First day of week').first().isVisible()), 'Other content not in defaults');
  assert.strictEqual(await page.locator('.component-text b').count(), 0, 'no bold header rows');
  assert.ok(await page.locator('.component-submit').isVisible(), 'Save bar visible while a section is open');

  await heading(page, 'Other').click();
  assert.ok(await page.getByText('First day of week').first().isVisible());
  assert.ok(await page.getByText('Date format').first().isVisible());
  assert.ok(await page.getByText('Alarm volume').first().isVisible());
  assert.ok(!(await visible(page, '.ad-sm')), 'defaults closed');
  assert.ok(await page.locator('.component-submit').isVisible(), 'Save bar visible');
  await heading(page, 'Other').click();
  assert.ok(await page.locator('.component-submit').isVisible(), 'Save bar visible with all closed');
});

test('"New alarm defaults" uses the same chips/grid layout as an alarm card', async () => {
  const { page } = await open();
  await heading(page, 'New alarm defaults').click();
  const chips = await page.locator('.ad-opts .al-chip span').allInnerTexts();
  assert.deepStrictEqual(chips, ['Vibration', 'Sound', 'Increasing']);
  const labels = await page.locator('.ad-rootc .al-lbl').allInnerTexts();
  assert.deepStrictEqual(labels, ['Vibration', 'Snooze (min)', 'Max snoozes']);
  // same visual treatment as the card's chips (checked chips are accent-coloured) and grid
  const bg = (sel) => page.locator(sel).first().evaluate((e) => getComputedStyle(e).backgroundColor);
  assert.strictEqual(await bg('.ad-opts .ad-vib + span'), 'rgb(255, 71, 0)', 'Vibration on by default');
  assert.strictEqual(await bg('.ad-opts .ad-inc + span'), 'rgb(102, 102, 102)', 'Increasing off by default');
  assert.strictEqual(await page.locator('.ad-rootc .al-grid').evaluate((e) => getComputedStyle(e).display), 'grid');
  // the 0-value notes, like the cards
  await page.fill('.ad-sm', '0');
  assert.strictEqual(await page.locator('.ad-sm-note').innerText(), 'snooze off');
  await page.fill('.ad-sx', '0');
  assert.strictEqual(await page.locator('.ad-sx-note').innerText(), 'unlimited');
});

test('a new alarm added on the page respects "New alarm defaults"', async () => {
  const { page } = await open();
  await heading(page, 'New alarm defaults').click();
  await page.fill('.ad-sm', '5');
  await page.fill('.ad-sx', '0');
  await page.locator('.ad-opts .al-chip', { hasText: 'Vibration' }).click();   // vibration off
  await page.locator('.ad-opts .al-chip', { hasText: 'Sound' }).click();       // sound off
  await page.locator('.ad-opts .al-chip', { hasText: 'Increasing' }).click();  // increasing on
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  assert.strictEqual(await page.locator('.al-sm').inputValue(), '5');
  assert.strictEqual(await page.locator('.al-sx').inputValue(), '0');
  assert.strictEqual(await page.locator('.al-vib').isChecked(), false);
  assert.strictEqual(await page.locator('.al-snd').isChecked(), false);
  assert.strictEqual(await page.locator('.al-inc').isChecked(), true);

  // and changing the defaults again affects only alarms added afterwards
  await heading(page, 'New alarm defaults').click();
  await page.fill('.ad-sm', '12');
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  assert.strictEqual(await page.locator('.al-sm').nth(0).inputValue(), '5', 'existing alarm unchanged');
  assert.strictEqual(await page.locator('.al-sm').nth(1).inputValue(), '12');
});

test('Save flattens the defaults for the watch; legacy flat settings are migrated into the new section', async () => {
  const legacy = JSON.stringify({ DefaultSnoozeEnabled: true, DefaultSnoozeMinutes: '15', DefaultSnoozeMax: '5',
    DefaultSoundEnabled: false, DefaultVibrationEnabled: true, DefaultIncreasingVolume: true, AlarmVibePattern: '1', AudioVolume: 30 });
  const t = await open({ 'clay-settings': legacy });
  await heading(t.page, 'New alarm defaults').click();
  assert.strictEqual(await t.page.locator('.ad-sm').inputValue(), '15');
  assert.strictEqual(await t.page.locator('.ad-sx').inputValue(), '5');
  assert.strictEqual(await t.page.locator('.ad-snd').isChecked(), false);
  assert.strictEqual(await t.page.locator('.ad-inc').isChecked(), true);
  assert.strictEqual(await t.page.locator('.ad-vp').inputValue(), '1');
  await t.page.fill('.ad-sm', '20');
  const sent = await t.save();
  assert.deepStrictEqual(JSON.parse(JSON.stringify(sent[0])), {
    FirstDayOfWeek: 1, DateFormat: 0, AlarmVibePattern: 1, DefaultSnoozeMinutes: 20, DefaultSnoozeMax: 5,
    DefaultSoundEnabled: 0, DefaultVibrationEnabled: 1, DefaultIncreasingVolume: 1, AudioVolume: 30,
  });
});

test('a plain launch sends the saved defaults (and migrates legacy ones)', async () => {
  const phone = loadPhone({ 'clay-settings': JSON.stringify({ DefaultSnoozeMinutes: '8', AlarmVibePattern: '2' }) });
  phone.fire('ready');
  assert.strictEqual(phone.sent[0].DefaultSnoozeMinutes, 8);
  assert.strictEqual(phone.sent[0].AlarmVibePattern, 2);
  const fresh = loadPhone();
  fresh.fire('ready');
  assert.strictEqual(fresh.sent[0].DefaultSnoozeMinutes, 10, 'fresh install sends the real defaults');
  assert.strictEqual(fresh.sent[0].DefaultSoundEnabled, 1);
});

test('alarms: add, Basic/Cron switch, validation tag, delete', async () => {
  const { page } = await open();
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  assert.strictEqual(await page.locator('.al-card').count(), 1);
  assert.ok(await visible(page, '.al-body'), 'a new alarm opens ready to edit');
  assert.ok(await visible(page, '.al-time'), 'Basic shows time');
  assert.ok(!(await visible(page, '.al-cronin')), 'Basic hides cron field');

  await page.click('.al-seg[data-v="1"]');
  assert.ok(!(await visible(page, '.al-time')), 'Cron hides time');
  assert.ok(!(await visible(page, '.al-days')), 'Cron hides weekday chips');
  assert.ok(await visible(page, '.al-cronin'), 'Cron shows cron field');
  assert.strictEqual(await page.locator('.al-seg.on').getAttribute('data-v'), '1');

  await page.fill('.al-cronin', '*/20 99');
  assert.ok((await page.locator('.al-err').innerText()).includes('Invalid'), 'invalid tag shown');
  assert.strictEqual(await page.locator('.al-next').innerText(), '', 'no preview for an invalid pattern');
  await page.fill('.al-cronin', '*/20 9-17 * * 1-5');
  assert.strictEqual(await page.locator('.al-err').innerText(), '', 'tag cleared when valid');
  assert.ok((await page.locator('.al-next').innerText()).startsWith('Next: '), 'preview shown');

  await page.click('.al-add');
  assert.strictEqual(await page.locator('.al-card').count(), 2);
  await page.locator('.al-del').first().click();
  assert.strictEqual(await page.locator('.al-card').count(), 1);
});

test('alarm cards are collapsed summaries that expand on tap', async () => {
  const { page } = await open(undefined, (phone) => snapshot(phone, snapAlarms, snapTunes));
  await heading(page, 'Alarms').click();
  assert.ok(!(await visible(page, '.al-body')), 'collapsed by default');
  const mains = await page.locator('.al-sum-main').allInnerTexts();
  const subs = await page.locator('.al-sum-sub').allInnerTexts();
  assert.deepStrictEqual(mains, ['06:30 \u00b7 Wake', 'Cronny']);
  assert.deepStrictEqual(subs, ['Mon\u2013Fri', '*/20 9-17 * * 1-5 *']);

  await page.locator('.al-sum').nth(0).click();
  assert.ok(await page.locator('.al-body').nth(0).isVisible(), 'tap expands');
  assert.ok(!(await page.locator('.al-body').nth(1).isVisible()), 'other card stays collapsed');
  await page.locator('.al-sum').nth(0).click();
  assert.ok(!(await page.locator('.al-body').nth(0).isVisible()), 'tap again collapses');

  // The on/off switch toggles enabled and dims the summary without expanding.
  assert.ok(await page.locator('.al-en').nth(0).isChecked());
  await page.locator('.al-switch').nth(0).click();
  assert.ok(!(await page.locator('.al-en').nth(0).isChecked()));
  assert.ok(!(await page.locator('.al-body').nth(0).isVisible()), 'switch does not expand');
  assert.ok(await page.locator('.al-card').nth(0).evaluate((e) => e.classList.contains('off')), 'dimmed when off');
});

test('expand triangle is a large target clear of the on/off switch', async () => {
  const { page } = await open(undefined, (phone) => snapshot(phone, snapAlarms, snapTunes));
  await heading(page, 'Alarms').click();
  const chev = await page.locator('.al-chev').first().boundingBox();
  const sw = await page.locator('.al-switch').first().boundingBox();
  assert.ok(chev.width >= 40 && chev.height >= 40, `chevron target ${chev.width}x${chev.height}`);
  assert.ok(chev.x >= sw.x + sw.width + 4, 'chevron target starts to the right of the switch');
  // Pressing the very left edge of the triangle's area still expands, and leaves the switch alone.
  const before = await page.locator('.al-en').first().isChecked();
  await page.mouse.click(chev.x + 2, chev.y + chev.height / 2);
  assert.ok(await page.locator('.al-body').first().isVisible(), 'chevron press expands');
  assert.strictEqual(await page.locator('.al-en').first().isChecked(), before, 'switch untouched');
});

test('summary follows edits live; expanded state survives reordering weekday setting', async () => {
  const { page } = await open(undefined, (phone) => snapshot(phone, snapAlarms, snapTunes));
  await heading(page, 'Alarms').click();
  await page.locator('.al-sum').nth(0).click();
  await page.locator('.al-name').nth(0).fill('Gym');
  await page.locator('.al-time').nth(0).fill('07:15');
  assert.strictEqual(await page.locator('.al-sum-main').nth(0).innerText(), '07:15 \u00b7 Gym');
  // weekday chips start on Monday by default; untick Mon..Fri, tick Sat+Sun
  const chips = await page.locator('.al-card').nth(0).locator('.al-chip span').allInnerTexts();
  assert.deepStrictEqual(chips.slice(0, 7), ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su']);
  for (const d of ['Mo', 'Tu', 'We', 'Th', 'Fr']) { await page.locator('.al-card').nth(0).locator('.al-days .al-chip', { hasText: d }).click(); }
  assert.strictEqual(await page.locator('.al-sum-sub').nth(0).innerText(), 'Once');
  for (const d of ['Sa', 'Su']) { await page.locator('.al-card').nth(0).locator('.al-days .al-chip', { hasText: d }).click(); }
  assert.strictEqual(await page.locator('.al-sum-sub').nth(0).innerText(), 'Weekends');

  // First day of week = Sunday reorders the chips in place, keeping values and open state.
  await heading(page, 'Other').click();
  await page.getByLabel('Sunday').check();
  await heading(page, 'Alarms').click();
  const after = await page.locator('.al-card').nth(0).locator('.al-days .al-chip span').allInnerTexts();
  assert.deepStrictEqual(after, ['Su', 'Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa']);
  assert.ok(await page.locator('.al-body').nth(0).isVisible(), 'card still open');
  assert.strictEqual(await page.locator('.al-name').nth(0).inputValue(), 'Gym');
  assert.strictEqual(await page.locator('.al-sum-sub').nth(0).innerText(), 'Weekends');
});

test('snooze fields explain their 0 values', async () => {
  const { page } = await open();
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  assert.strictEqual(await page.locator('.al-sm-note').innerText(), '');
  await page.fill('.al-sm', '0');
  assert.strictEqual(await page.locator('.al-sm-note').innerText(), 'snooze off');
  await page.fill('.al-sx', '0');
  assert.strictEqual(await page.locator('.al-sx-note').innerText(), 'unlimited');
  await page.fill('.al-sx', '3');
  assert.strictEqual(await page.locator('.al-sx-note').innerText(), '');
});

test('cron preview shows the next ring time (fixed clock: Mon 9 Nov 2026 10:05)', async () => {
  const phone = loadPhone();
  const html = openConfigPage(phone);
  const page = await browser.newPage({ viewport: { width: 390, height: 844 } });
  await page.clock.install({ time: new Date(2026, 10, 9, 10, 5, 0) });
  await page.setContent(html);
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  await page.click('.al-seg[data-v="1"]');
  const cases = [
    ['*/20 9-17 * * 1-5', 'Next: today 10:20'],
    ['0 7 * * *', 'Next: tomorrow 07:00'],
    ['0 8 * * 0', 'Next: Sun 08:00'],
    ['0 12 1 * *', 'Next: 1.12. 12:00'],
    ['0 0 30 2 *', 'Next: never (nothing within a year)'],
    ['0 9 * * 1 */2', 'Next: Mon 09:00'],
  ];
  for (const [pat, want] of cases) {
    await page.fill('.al-cronin', pat);
    assert.strictEqual(await page.locator('.al-next').innerText(), want, pat);
  }
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
  // the watch's 160-character cap
  const base = new Array(22).fill("^C'7/8").join(' ');
  await page.fill('.tu-pat', base + " ^C'7/8");
  assert.strictEqual(await page.locator('.tu-err').innerText(), '', '160 characters is fine');
  await page.fill('.tu-pat', base + ' ^C,,7/8');
  assert.ok((await page.locator('.tu-err').innerText()).includes('160'), '161 characters is flagged');
  await page.locator('.tu-del').click();
  assert.strictEqual(await page.locator('.tu-row').count(), 0);
});

test('max rows hides the Add button (16 alarms, 20 tunes)', async () => {
  const { page } = await open();
  await heading(page, 'Alarms').click();
  for (let i = 0; i < 16; i++) { await page.click('.al-add'); }
  assert.strictEqual(await page.locator('.al-card').count(), 16);
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
  await page.locator('.al-days .al-chip', { hasText: 'Mo' }).click();
  await page.locator('.al-days .al-chip', { hasText: 'Fr' }).click();
  await page.locator('.al-tune').dispatchEvent('mousedown');
  await page.selectOption('.al-tune', { label: 'Mine' });
  await page.click('.al-add');
  await page.locator('.al-name').nth(1).fill('Cron');
  await page.locator('.al-card').nth(1).locator('.al-seg[data-v="1"]').click();
  await page.locator('.al-cronin').nth(1).fill('*/20 9-17');
  await page.click('.al-add');   // third alarm: invalid cron, must be dropped
  await page.locator('.al-card').nth(2).locator('.al-seg[data-v="1"]').click();
  await page.locator('.al-cronin').nth(2).fill('bogus');

  const sent = await t.save();
  const keys = sent.map((d) => Object.keys(d)[0]);
  assert.deepStrictEqual(keys, ['FirstDayOfWeek', 'VibeConfig', 'TuneConfig', 'AlarmConfig']);
  const tunes = sent[2].TuneConfig.split('\x1e');
  assert.strictEqual(tunes.length, 1, 'invalid tune dropped');
  const [tid, tname, tpat] = tunes[0].split('\x1f');
  assert.strictEqual(tname, 'Mine'); assert.strictEqual(tpat, 'C z E');
  assert.ok(parseInt(tid, 10) >= 0x10000);
  const alarms = sent[3].AlarmConfig.split('\x1e').map((r) => r.split('\x1f'));
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

// ---- watch snapshot -> page ----

const RS = '\x1e', US = '\x1f';
const snapTunes = ['3', 'Watch tune', 'C z E', '4', 'Other tune', 'G A B'].reduce((acc, v, i, a) => (i % 3 === 0 ? acc.concat([a.slice(i, i + 3).join(US)]) : acc), []).join(RS);
const snapAlarms = [
  ['1', 'Wake', '1', '0', '6', '30', '62', '*', '10', '3', '1', '1', '3', '0', '0'],
  ['2', 'Cronny', '0', '1', '7', '0', '0', '*/20 9-17 * * 1-5 *', '0', '0', '1', '0', '0', '1', '1'],
].map((r) => r.join(US)).join(RS);
const snapshot = (phone, alarms, tunes) => phone.fire('appmessage', { payload: { TuneConfig: tunes, AlarmConfig: alarms } });

test('watch snapshot fills the page with existing alarms and tunes', async () => {
  const { page, errors } = await open(undefined, (phone) => snapshot(phone, snapAlarms, snapTunes));
  await heading(page, 'Alarms').click();
  assert.strictEqual(await page.locator('.al-card').count(), 2);
  assert.strictEqual(await page.locator('.al-name').nth(0).inputValue(), 'Wake');
  assert.strictEqual(await page.locator('.al-time').nth(0).inputValue(), '06:30');
  assert.strictEqual(await page.locator('.al-d:checked').count(), 5, 'Mon-Fri ticked');
  assert.strictEqual(await page.locator('.al-tune').nth(0).evaluate((e) => e.options[e.selectedIndex].text), 'Watch tune');
  assert.strictEqual(await page.locator('.al-card').nth(1).locator('.al-seg.on').getAttribute('data-v'), '1');
  assert.strictEqual(await page.locator('.al-cronin').nth(1).inputValue(), '*/20 9-17 * * 1-5 *');
  assert.strictEqual(await page.locator('.al-en').nth(1).isChecked(), false);
  assert.strictEqual(await page.locator('.al-ad').nth(1).inputValue(), '1');
  await heading(page, 'Tunes').click();
  assert.strictEqual(await page.locator('.tu-row').count(), 2);
  assert.strictEqual(await page.locator('.tu-pat').nth(0).inputValue(), 'C z E');
  assert.deepStrictEqual(errors, []);
});

test('a later snapshot replaces the stored lists', async () => {
  const phone = loadPhone();
  snapshot(phone, snapAlarms, snapTunes);
  snapshot(phone, snapAlarms.split(RS)[0], '');
  const cs = JSON.parse(phone.store['clay-settings']);
  assert.strictEqual(cs.AlarmList.length, 1);
  assert.strictEqual(cs.TuneList.length, 0);
});

test('snapshots are ignored while page edits are pending delivery', async () => {
  const t = await open(undefined, (phone) => snapshot(phone, snapAlarms, snapTunes));
  await heading(t.page, 'Alarms').click();
  await t.page.locator('.al-sum').nth(0).click();
  await t.page.locator('.al-name').nth(0).fill('Edited');
  t.phone.failSends = true;
  await t.save();
  assert.strictEqual(t.phone.store.lists_pending, '1');
  snapshot(t.phone, snapAlarms.split(RS)[1], snapTunes);   // watch (stale) snapshot arrives
  const cs = JSON.parse(t.phone.store['clay-settings']);
  assert.strictEqual(cs.AlarmList.length, 2, 'unsent edits not overwritten');
  assert.strictEqual(cs.AlarmList[0].name, 'Edited');
});

test('deleting a snapshot item on the page sends an id-only tombstone, kept until delivered', async () => {
  const t = await open(undefined, (phone) => snapshot(phone, snapAlarms, snapTunes));
  await heading(t.page, 'Alarms').click();
  await t.page.locator('.al-sum').nth(0).click();
  await t.page.locator('.al-del').nth(0).click();   // alarm 1 ("Wake")
  await heading(t.page, 'Tunes').click();
  await t.page.locator('.tu-del').nth(1).click();   // tune 4
  t.phone.failSends = true;
  await t.save();
  assert.deepStrictEqual(JSON.parse(t.phone.store.alarm_tombstones), [1]);
  assert.deepStrictEqual(JSON.parse(t.phone.store.tune_tombstones), [4]);

  t.phone.failSends = false;   // next launch: pending push goes out with the tombstones
  t.phone.sent.length = 0;
  t.phone.fire('ready');
  const tunes = t.phone.sent.find((d) => 'TuneConfig' in d).TuneConfig.split(RS);
  const alarms = t.phone.sent.find((d) => 'AlarmConfig' in d).AlarmConfig.split(RS);
  assert.ok(tunes.includes('4'), 'tune tombstone is a bare id: ' + JSON.stringify(tunes));
  assert.ok(alarms.includes('1'), 'alarm tombstone is a bare id: ' + JSON.stringify(alarms));
  assert.strictEqual(alarms.length, 2, 'remaining alarm + tombstone');
  assert.strictEqual(t.phone.store.alarm_tombstones, undefined, 'cleared after delivery');
  assert.strictEqual(t.phone.store.tune_tombstones, undefined);
});

test('items that appear on the watch after the page opened are not tombstoned', async () => {
  const t = await open(undefined, (phone) => snapshot(phone, snapAlarms, snapTunes));
  const extra = ['9', 'New on watch', '1', '0', '8', '0', '2', '*', '10', '3', '1', '1', '0', '0', '0'].join(US);
  snapshot(t.phone, snapAlarms + RS + extra, snapTunes);   // arrives while the page is open
  await heading(t.page, 'Alarms').click();
  await t.page.locator('.al-sum').nth(1).click();
  await t.page.locator('.al-del').nth(1).click();   // delete alarm 2 on the page
  await t.save();
  const sent = t.phone.sent.find((d) => 'AlarmConfig' in d).AlarmConfig.split(RS);
  assert.ok(sent.includes('2'), 'deleted alarm tombstoned');
  assert.ok(!sent.includes('9'), 'watch-created alarm 9 not tombstoned');
});

test('a plain unchanged Save does not tombstone anything', async () => {
  const t = await open(undefined, (phone) => snapshot(phone, snapAlarms, snapTunes));
  await t.save();
  const alarms = t.phone.sent.find((d) => 'AlarmConfig' in d).AlarmConfig.split(RS);
  assert.strictEqual(alarms.length, 2);
  assert.ok(alarms.every((r) => r.split(US).length === 17));
});

// ---- ordering (same as the watch's main list) ----

const orderAlarms = [
  ['10', 'Nine', '1', '0', '9', '0', '0', '*', '10', '3', '1', '1', '0', '0', '0'],
  ['11', 'Six A', '1', '0', '6', '30', '0', '*', '10', '3', '1', '1', '0', '0', '0'],
  ['12', 'Cron early', '1', '1', '7', '0', '0', '*/20 5-7 * * * *', '10', '3', '1', '1', '0', '0', '0'],
  ['13', 'Six B', '1', '0', '6', '30', '0', '*', '10', '3', '1', '1', '0', '0', '0'],
].map((r) => r.join(US)).join(RS);

test('alarms are ordered by clock time like the watch: cron by earliest match, ties keep list order', async () => {
  const { page } = await open(undefined, (phone) => snapshot(phone, orderAlarms, ''));
  await heading(page, 'Alarms').click();
  const names = async () => page.locator('.al-name').evaluateAll((els) => els.map((e) => e.value));
  // 05:00 (cron: first hour 5, first minute 0), 06:30 Six A, 06:30 Six B (tie: list order), 09:00
  assert.deepStrictEqual(await names(), ['Cron early', 'Six A', 'Six B', 'Nine']);
});

test('re-sorting waits until a card is collapsed, and a new alarm stays last while editing', async () => {
  const { page } = await open(undefined, (phone) => snapshot(phone, orderAlarms, ''));
  await heading(page, 'Alarms').click();
  const names = async () => page.locator('.al-name').evaluateAll((els) => els.map((e) => e.value));

  await page.locator('.al-sum').nth(3).click();                        // open "Nine"
  await page.locator('.al-time').nth(3).fill('04:00');
  assert.deepStrictEqual(await names(), ['Cron early', 'Six A', 'Six B', 'Nine'], 'no jump while editing');
  assert.strictEqual(await page.locator('.al-sum-main').nth(3).innerText(), '04:00 · Nine');
  await page.locator('.al-sum').nth(3).click();                        // collapse -> settle into order
  assert.deepStrictEqual(await names(), ['Nine', 'Cron early', 'Six A', 'Six B']);

  await page.click('.al-add');                                          // new alarm, default 07:00
  await page.locator('.al-name').nth(4).fill('New');
  assert.deepStrictEqual(await names(), ['Nine', 'Cron early', 'Six A', 'Six B', 'New'], 'new alarm stays at the bottom while open');
  await page.locator('.al-sum').nth(4).click();
  assert.deepStrictEqual(await names(), ['Nine', 'Cron early', 'Six A', 'Six B', 'New'], '07:00 sorts after the 06:30s');

  await page.locator('.al-sum').nth(4).click();                         // reopen New, give it 05:30, collapse
  await page.locator('.al-time').nth(4).fill('05:30');
  await page.locator('.al-sum').nth(4).click();
  assert.deepStrictEqual(await names(), ['Nine', 'Cron early', 'New', 'Six A', 'Six B']);
});

// ---- vibration patterns library ----

const snapVibes = [['5', 'Heartbeat', '1 z/2 1 z 3'], ['6', 'Long buzz', '8']].map((r) => r.join(US)).join(RS);
const snapAlarmsVibe = [
  ['1', 'Uses library', '1', '0', '6', '30', '62', '*', '10', '3', '1', '1', '0', '0', '0', '5', '1'],
  ['2', 'Uses preset', '1', '0', '7', '0', '0', '*', '10', '3', '1', '1', '0', '0', '0', '0', '2'],
].map((r) => r.join(US)).join(RS);
const snapshotAll = (phone, vibes, tunes, alarms) =>
  phone.fire('appmessage', { payload: { VibeConfig: vibes, TuneConfig: tunes, AlarmConfig: alarms } });

test('vibration patterns: add, validation tag, delete, row limit', async () => {
  const { page, errors } = await open();
  await heading(page, 'Vibration patterns').click();
  assert.ok(await visible(page, '.vb-add'));
  assert.ok(!(await visible(page, '.al-add')), 'other sections stay closed');
  await page.click('.vb-add');
  for (const bad of ['C', '9', '1 1 1 1 1 1 1 1 1', 'z9', '2  x']) {
    await page.fill('.vb-pat', bad);
    assert.ok((await page.locator('.vb-err').innerText()).includes('Invalid'), 'invalid: ' + bad);
  }
  for (const ok of ['2 z 2', '1/2', '/2 z/2 8', ' 2 ', '1 1 1 1 1 1 1 1']) {
    await page.fill('.vb-pat', ok);
    assert.strictEqual(await page.locator('.vb-err').innerText(), '', 'valid: ' + ok);
  }
  await page.locator('.vb-del').click();
  assert.strictEqual(await page.locator('.vb-row').count(), 0);
  for (let i = 0; i < 20; i++) { await page.click('.vb-add'); }
  assert.ok(!(await visible(page, '.vb-add')), 'Add hidden at 20');
  assert.deepStrictEqual(errors, []);
});

test('alarm card has a Vibration select: Double/Short/Long plus only valid library vibrations', async () => {
  const { page } = await open();
  await heading(page, 'Vibration patterns').click();
  await page.click('.vb-add');
  await page.locator('.vb-name').nth(0).fill('Heartbeat'); await page.locator('.vb-pat').nth(0).fill('1 z/2 1');
  await page.click('.vb-add');
  await page.locator('.vb-name').nth(1).fill('Broken'); await page.locator('.vb-pat').nth(1).fill('C');
  await page.click('.vb-add');
  await page.locator('.vb-pat').nth(2).fill('2');   // valid but unnamed
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  await page.locator('.al-vibsel').dispatchEvent('mousedown');
  assert.deepStrictEqual(await page.locator('.al-vibsel option').allInnerTexts(), ['Double', 'Short', 'Long', 'Heartbeat']);
  // fixing the pattern makes it appear live
  await heading(page, 'Vibration patterns').click();
  await page.locator('.vb-pat').nth(1).fill('2 z 2');
  await page.locator('.vb-pat').nth(1).dispatchEvent('change');
  await heading(page, 'Alarms').click();
  await page.locator('.al-vibsel').dispatchEvent('mousedown');
  assert.deepStrictEqual(await page.locator('.al-vibsel option').allInnerTexts(), ['Double', 'Short', 'Long', 'Heartbeat', 'Broken']);
});

test('a new alarm takes its vibration preset from "New alarm defaults"', async () => {
  const { page } = await open();
  await heading(page, 'New alarm defaults').click();
  await page.selectOption('.ad-vp', '2');   // Long
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  assert.strictEqual(await page.locator('.al-vibsel').inputValue(), 'p2');
});

test('Save sends VibeConfig first and the alarm\'s vibration choice (library id or preset)', async () => {
  const t = await open();
  const { page } = t;
  await heading(page, 'Vibration patterns').click();
  await page.click('.vb-add');
  await page.locator('.vb-name').nth(0).fill('Heartbeat'); await page.locator('.vb-pat').nth(0).fill('1 z/2 1');
  await page.click('.vb-add');
  await page.locator('.vb-name').nth(1).fill('Nope'); await page.locator('.vb-pat').nth(1).fill('Q');
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  await page.locator('.al-vibsel').dispatchEvent('mousedown');
  await page.selectOption('.al-vibsel', { label: 'Heartbeat' });
  await page.click('.al-add');
  await page.locator('.al-vibsel').nth(1).selectOption('p1');   // Short
  const sent = await t.save();
  assert.deepStrictEqual(sent.map((d) => Object.keys(d)[0]), ['FirstDayOfWeek', 'VibeConfig', 'TuneConfig', 'AlarmConfig']);
  const vibes = sent[1].VibeConfig.split(RS);
  assert.strictEqual(vibes.length, 1, 'invalid vibration dropped');
  const [vid, vname, vpat] = vibes[0].split(US);
  assert.strictEqual(vname, 'Heartbeat'); assert.strictEqual(vpat, '1 z/2 1');
  assert.ok(parseInt(vid, 10) >= 0x10000);
  const alarms = sent[3].AlarmConfig.split(RS).map((r) => r.split(US));
  assert.strictEqual(alarms[0][15], vid, 'alarm 1 references the library vibration');
  assert.strictEqual(alarms[1][15], '0');
  assert.strictEqual(alarms[1][16], '1', 'alarm 2 uses the Short preset');
});

test('watch snapshot fills Vibration patterns and each alarm\'s Vibration select', async () => {
  const { page, errors } = await open(undefined, (phone) => snapshotAll(phone, snapVibes, '', snapAlarmsVibe));
  await heading(page, 'Vibration patterns').click();
  assert.strictEqual(await page.locator('.vb-row').count(), 2);
  assert.strictEqual(await page.locator('.vb-name').nth(0).inputValue(), 'Heartbeat');
  assert.strictEqual(await page.locator('.vb-pat').nth(0).inputValue(), '1 z/2 1 z 3');
  await heading(page, 'Alarms').click();
  const sel = async (n) => page.locator('.al-vibsel').nth(n).evaluate((e) => e.options[e.selectedIndex].text);
  assert.strictEqual(await sel(0), 'Heartbeat');
  assert.strictEqual(await sel(1), 'Long');
  assert.deepStrictEqual(errors, []);
});

test('deleting a library vibration sends a tombstone and alarms using it fall back to a preset', async () => {
  const t = await open(undefined, (phone) => snapshotAll(phone, snapVibes, '', snapAlarmsVibe));
  await heading(t.page, 'Vibration patterns').click();
  await t.page.locator('.vb-del').nth(0).click();   // vibration 5, used by alarm 1
  await heading(t.page, 'Alarms').click();
  const sel = await t.page.locator('.al-vibsel').nth(0).evaluate((e) => e.options[e.selectedIndex].text);
  assert.strictEqual(sel, '(missing vibration)', 'the page flags the dangling reference');
  const sent = await t.save();
  const vibes = sent.find((d) => 'VibeConfig' in d).VibeConfig.split(RS);
  assert.ok(vibes.includes('5'), 'vibration tombstone is a bare id: ' + JSON.stringify(vibes));
  const alarms = sent.find((d) => 'AlarmConfig' in d).AlarmConfig.split(RS).map((r) => r.split(US));
  assert.strictEqual(alarms[0][15], '0', 'the alarm no longer references the deleted vibration');
});

test('a snapshot without VibeConfig (older watch) leaves the stored vibration list alone', async () => {
  const phone = loadPhone();
  snapshotAll(phone, snapVibes, '', snapAlarmsVibe);
  phone.fire('appmessage', { payload: { AlarmConfig: snapAlarmsVibe.split(RS)[0] } });
  const cs = JSON.parse(phone.store['clay-settings']);
  assert.strictEqual(cs.VibeList.length, 2);
  assert.strictEqual(cs.AlarmList.length, 1);
});

// ---- read-only built-ins and the collapsible long help ----

const TUNE_BUILTIN_NAMES = ['Triple', 'Double', 'Triple slow', 'Analog bell', 'Celebration', 'Lullaby', 'March'];

test('Tunes lists the watch\'s built-in tunes read-only; they are not part of the saved lists', async () => {
  const t = await open();
  const { page, errors } = t;
  await heading(page, 'Tunes').click();
  assert.deepStrictEqual(await page.locator('.tu-biname').evaluateAll((els) => els.map((e) => e.textContent)), TUNE_BUILTIN_NAMES);
  assert.strictEqual(await page.locator('.tu-bipat').nth(0).inputValue(), 'C z C z C');
  assert.strictEqual(await page.locator('.tu-bipat').nth(5).inputValue(), 'G,8 _B,4 A,2 G,2 A,4 D4 D,6');
  assert.strictEqual(await page.locator('.tu-bi input').evaluateAll((els) => els.every((e) => e.readOnly)), true, 'all read-only');
  assert.strictEqual(await page.locator('.tu-row').count(), 0, 'built-ins are not editable rows');
  // typing into a built-in changes nothing
  await page.locator('.tu-bipat').nth(0).click();
  await page.keyboard.type('XYZ');
  assert.strictEqual(await page.locator('.tu-bipat').nth(0).inputValue(), 'C z C z C');
  // adding/removing the user's own tunes is unaffected, and Save sends only those
  await page.click('.tu-add');
  await page.locator('.tu-name').fill('Mine'); await page.locator('.tu-pat').fill('C E G');
  const sent = await t.save();
  const tunes = sent.find((d) => 'TuneConfig' in d).TuneConfig.split(RS);
  assert.strictEqual(tunes.length, 1);
  assert.strictEqual(tunes[0].split(US)[1], 'Mine');
  assert.ok(!sent.find((d) => 'TuneConfig' in d).TuneConfig.includes('Triple'), 'no built-in leaks into the push');
  assert.deepStrictEqual(errors, []);
});

test('Vibration patterns lists Double/Short/Long read-only', async () => {
  const { page } = await open();
  await heading(page, 'Vibration patterns').click();
  assert.deepStrictEqual(await page.locator('.vb-biname').evaluateAll((els) => els.map((e) => e.textContent)), ['Double', 'Short', 'Long']);
  assert.deepStrictEqual(await page.locator('.vb-bipat').evaluateAll((els) => els.map((e) => e.value)), ['2 z 2', '2', '8']);
  assert.strictEqual(await page.locator('.vb-bi input').evaluateAll((els) => els.every((e) => e.readOnly)), true);
  assert.strictEqual(await page.locator('.vb-row').count(), 0);
});

test('tapping a built-in value selects all of it, ready to copy (there is no Copy button)', async () => {
  const { page } = await open();
  await heading(page, 'Tunes').click();
  const el = page.locator('.tu-bipat').nth(3);   // "C G C G C G C G"
  await el.click();
  const sel = await el.evaluate((e) => [e.selectionStart, e.selectionEnd, e.value.length]);
  assert.deepStrictEqual(sel, [0, sel[2], sel[2]]);
  assert.ok(sel[2] > 0);
  assert.strictEqual(await page.locator('.tu-copy, .vb-copy').count(), 0);
  assert.strictEqual(await page.locator('.tu-bi button').count(), 0);
});

test('the long syntax help is collapsible (collapsed by default) and replaces the short instruction', async () => {
  const { page } = await open();
  await heading(page, 'Tunes').click();
  assert.strictEqual(await page.locator('.tu-helpbox').evaluate((e) => e.open), false, 'collapsed by default');
  assert.ok(!(await page.getByText('A tune is a list of notes and rests').isVisible()), 'text hidden while collapsed');
  await page.locator('.tu-helpbox summary').click();
  assert.strictEqual(await page.locator('.tu-helpbox').evaluate((e) => e.open), true);
  assert.ok(await page.getByText('A tune is a list of notes and rests').isVisible());
  assert.ok(await page.getByText('A rest is "z" followed by the same optional ratio').first().isVisible(), 'long help body present');
  assert.strictEqual(await page.locator('.tu-helptext p').count() >= 5, true, 'paragraphs preserved');
  await page.locator('.tu-helpbox summary').click();
  assert.ok(!(await page.getByText('A tune is a list of notes and rests').isVisible()), 'collapses again');
  // the old one-line instruction is gone
  assert.strictEqual(await page.getByText('Notes C D E F G A B, optional').count(), 0);

  await heading(page, 'Vibration patterns').click();
  assert.strictEqual(await page.locator('.vb-helpbox').evaluate((e) => e.open), false);
  await page.locator('.vb-helpbox summary').click();
  assert.ok(await page.getByText('A vibration pattern is a list of on-beats and rests').isVisible());
  assert.strictEqual(await page.getByText('Steps separated by spaces, up to 8').count(), 0, 'old short vibration instruction gone');
});

test('each built-in is one line: a plain-text label and one read-only value field', async () => {
  const { page } = await open();
  for (const [sec, p, n] of [['Tunes', 'tu', 7], ['Vibration patterns', 'vb', 3]]) {
    await heading(page, sec).click();
    const rows = page.locator(`.${p}-bi`);
    assert.strictEqual(await rows.count(), n);
    for (let i = 0; i < n; i++) {
      const row = rows.nth(i);
      const label = row.locator(`.${p}-biname`);
      assert.strictEqual(await label.evaluate((e) => e.tagName), 'SPAN', 'label is plain text, not a field');
      assert.strictEqual(await row.locator('input').count(), 1, 'exactly one input per row');
      assert.strictEqual(await row.locator('button').count(), 0);
      // label and value sit on the same line (same vertical band) and the row is one field tall
      const [lb, vb, rb] = await Promise.all([label.boundingBox(), row.locator('input').boundingBox(), row.boundingBox()]);
      assert.ok(Math.abs((lb.y + lb.height / 2) - (vb.y + vb.height / 2)) < 6, 'label and value are vertically aligned');
      assert.ok(vb.x > lb.x + lb.width - 2, 'value is to the right of the label');
      assert.ok(rb.height < 50, `row is one line tall (${rb.height}px)`);
    }
  }
});

// ---- full cron help under the Basic/Cron selector ----

test('cron alarms show the watch\'s full cron help, collapsible, under the Basic/Cron selector', async () => {
  const { page, errors } = await open();
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  // Basic mode: no cron help
  assert.ok(!(await visible(page, '.al-cronhelpbox')), 'hidden in Basic mode');
  await page.click('.al-seg[data-v="1"]');
  assert.ok(await visible(page, '.al-cronhelpbox'), 'shown in Cron mode');

  // placed right under the selector and above the cron field
  const seg = await page.locator('.al-seg-wrap').boundingBox();
  const box = await page.locator('.al-cronhelpbox').boundingBox();
  const field = await page.locator('.al-cronin').boundingBox();
  assert.ok(box.y >= seg.y + seg.height - 1, 'below the Basic/Cron selector');
  assert.ok(box.y + box.height <= field.y + 1, 'above the cron pattern field');

  // collapsed by default; expands to the full text, collapses again
  assert.strictEqual(await page.locator('.al-cronhelpbox').evaluate((e) => e.open), false);
  assert.ok(!(await page.getByText('Alarm fires every minute that matches every cron item').isVisible()));
  await page.locator('.al-cronhelpbox summary').click();
  assert.ok(await page.getByText('Alarm fires every minute that matches every cron item').isVisible());
  assert.ok(await page.getByText('Week (ISO week-of-year):').isVisible(), 'full text, not a summary');
  assert.ok(await page.getByText('up biweekly alarms.').isVisible(), 'down to the last line');
  assert.strictEqual(await page.locator('.al-cronhelptext p').count() >= 9, true, 'every paragraph present');
  await page.locator('.al-cronhelpbox summary').click();
  assert.strictEqual(await page.locator('.al-cronhelpbox').evaluate((e) => e.open), false);

  // the short syntax line under the cron field is gone
  assert.strictEqual(await page.getByText('weekday 0 = Sunday').count(), 0);
  assert.strictEqual(await page.locator('.al-cronhelp').count(), 0);
  assert.ok((await page.locator('.al-next').count()) === 1, 'the live Next: preview stays');
  assert.deepStrictEqual(errors, []);
});

test('an open cron help stays open when the cards re-render', async () => {
  const { page } = await open(undefined, (phone) => snapshot(phone, snapAlarms, snapTunes));
  await heading(page, 'Alarms').click();
  await page.locator('.al-sum').nth(1).click();                       // open the cron alarm
  await page.locator('.al-card').nth(1).locator('.al-cronhelpbox summary').click();
  assert.strictEqual(await page.locator('.al-card').nth(1).locator('.al-cronhelpbox').evaluate((e) => e.open), true);
  // collapsing the OTHER card re-sorts/re-renders every card
  await page.locator('.al-sum').nth(0).click();
  await page.locator('.al-sum').nth(0).click();
  assert.strictEqual(await page.locator('.al-card').nth(1).locator('.al-cronhelpbox').evaluate((e) => e.open), true, 'still open');
  assert.ok(await page.locator('.al-card').nth(1).locator('.al-cronhelptext').isVisible());
});

// ---- nothing touches the section outline ----

test('content in every section keeps a margin from the section outline (sides and top)', async () => {
  const { page } = await open(undefined, (phone) => {
    phone.fire('appmessage', { payload: { VibeConfig: ['5', 'Heartbeat', '1 z/2 1'].join(US), TuneConfig: ['3', 'Mine', 'C z E'].join(US), AlarmConfig: snapAlarms } });
  });
  const MIN = 8;   // px between any content and the section's left/right edge
  const sections = [['New alarm defaults', '.ad-rootc'], ['Alarms', '.al-rootc'], ['Tunes', '.tu-rootc'], ['Vibration patterns', '.vb-rootc']];
  for (const [name, rootSel] of sections) {
    await heading(page, name).click();
    if (name === 'Alarms') {
      await page.locator('.al-sum').nth(0).click();   // expanded content too
      await page.locator('.al-sum').nth(1).click();
      await page.locator('.al-card').nth(1).locator('.al-cronhelpbox summary').click();
    }
    if (name === 'Tunes' || name === 'Vibration patterns') { await page.locator(rootSel + ' details summary').click(); }
    // let the chevron's rotation settle: mid-transition its bounding box is wider than the card
    await page.evaluate(() => Promise.all(document.getAnimations().map((a) => a.finished)));
    const worst = await page.evaluate((sel) => {
      const root = document.querySelector(sel);
      const section = root.closest('.section');
      const sb = section.getBoundingClientRect();
      let left = Infinity, right = Infinity, who = '', whoR = '';
      root.querySelectorAll('*').forEach((el) => {
        const b = el.getBoundingClientRect();
        if (b.width === 0 || b.height === 0) { return; }
        if (el.offsetParent === null) { return; }
        if (el.scrollWidth > el.clientWidth + 1 && getComputedStyle(el).overflow !== 'visible') { return; }
        const l = b.left - sb.left, r = sb.right - b.right;
        if (l < left) { left = l; who = el.className || el.tagName; }
        if (r < right) { right = r; whoR = el.className || el.tagName; }
      });
      return { left: Math.round(left), right: Math.round(right), who, whoR };
    }, rootSel);
    // gap between the heading and the first piece of content (a control, a box or a text line)
    const topGap = await page.evaluate((nm) => {
      const h = [...document.querySelectorAll('.component-heading')].find((e) => e.textContent.toLowerCase().includes(nm.toLowerCase()));
      const sec = h.closest('.section');
      const hb = h.getBoundingClientRect();
      let top = Infinity;
      sec.querySelectorAll('input,select,button,details,.al-card').forEach((el) => {
        const b = el.getBoundingClientRect();
        if (el.offsetParent !== null && !h.contains(el) && b.width && b.height && b.top < top) { top = b.top; }
      });
      const tw = document.createTreeWalker(sec, NodeFilter.SHOW_TEXT);
      let n;
      while ((n = tw.nextNode())) {
        if (!n.textContent.trim() || h.contains(n) || !n.parentElement.offsetParent) { continue; }
        const r = document.createRange();
        r.selectNodeContents(n);
        const b = r.getBoundingClientRect();
        if (b.width && b.height && b.top < top) { top = b.top; }
      }
      return Math.round(top - hb.bottom);
    }, name);
    assert.ok(topGap >= MIN, `${name}: first content only ${topGap}px below the heading`);
    assert.ok(worst.left >= MIN, `${name}: content ${worst.left}px from the left edge (${worst.who})`);
    assert.ok(worst.right >= MIN, `${name}: content ${worst.right}px from the right edge (${worst.whoR})`);
    await heading(page, name).click();   // close before the next one
  }
});

// ---- grouped panels: Schedule / Signal / Snooze and dismiss ----

test('an alarm card groups its settings into Schedule, Signal and Snooze-and-dismiss panels', async () => {
  const { page } = await open();
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  const caps = await page.locator('.al-group .al-gcap').allInnerTexts();
  assert.deepStrictEqual(caps.map((c) => c.toLowerCase()), ['schedule', 'signal', 'snooze and dismiss']);

  const inGroup = async (group, sel) => page.locator(`.al-g-${group} ${sel}`).count();
  // everything that decides WHEN is in Schedule ...
  for (const sel of ['.al-seg-wrap', '.al-time', '.al-days', '.al-cronin', '.al-cronhelpbox', '.al-next']) {
    assert.strictEqual(await inGroup('sched', sel), 1, sel + ' belongs to Schedule');
  }
  // ... HOW it rings in Signal ...
  for (const sel of ['.al-vib', '.al-snd', '.al-inc', '.al-vibsel', '.al-tune']) {
    assert.strictEqual(await inGroup('signal', sel), 1, sel + ' belongs to Signal');
  }
  // ... and what happens afterwards in Snooze and dismiss
  for (const sel of ['.al-sm', '.al-sx', '.al-ad']) {
    assert.strictEqual(await inGroup('snooze', sel), 1, sel + ' belongs to Snooze and dismiss');
  }
  // the label sits above the panels, outside all of them
  assert.strictEqual(await page.locator('.al-group .al-name').count(), 0);
  const label = await page.locator('.al-name').boundingBox();
  const first = await page.locator('.al-g-sched').boundingBox();
  assert.ok(label.y + label.height <= first.y, 'label above the first panel');
  // panels are stacked in order and visually bounded (a border and a distinct background)
  const boxes = await page.locator('.al-group').evaluateAll((els) => els.map((e) => {
    const b = e.getBoundingClientRect(); const c = getComputedStyle(e);
    return { top: b.top, bottom: b.bottom, border: c.borderTopWidth, bg: c.backgroundColor };
  }));
  assert.ok(boxes[0].bottom <= boxes[1].top && boxes[1].bottom <= boxes[2].top, 'stacked top to bottom');
  assert.ok(boxes.every((b) => b.border === '1px'), 'each panel has an outline');
  const capColors = await page.locator('.al-gcap').evaluateAll((els) => els.map((e) => getComputedStyle(e).color));
  assert.deepStrictEqual(capColors, ['rgb(255, 255, 255)', 'rgb(255, 255, 255)', 'rgb(255, 255, 255)'], 'panel headers are white');
  const cardBg = await page.locator('.al-card').evaluate((e) => getComputedStyle(e).backgroundColor);
  assert.ok(boxes.every((b) => b.bg !== cardBg), 'panels are tinted differently from the card');
});

test('the Schedule panel ends with the schedule in words and when it next rings', async () => {
  const phone = loadPhone();
  const html = openConfigPage(phone);
  const page = await browser.newPage({ viewport: { width: 390, height: 844 } });
  await page.clock.install({ time: new Date(2026, 10, 9, 10, 5, 0) });   // Monday 10:05
  await page.setContent(html);
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  const set = async (time, days) => {
    await page.fill('.al-time', time);
    for (const d of ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su']) {
      const chip = page.locator('.al-days .al-chip', { hasText: d });
      const on = await chip.locator('input').isChecked();
      if (on !== days.includes(d)) { await chip.click(); }
    }
  };
  const foot = () => page.locator('.al-next').innerText();
  await set('06:30', ['Mo', 'Tu', 'We', 'Th', 'Fr']);
  assert.strictEqual(await foot(), 'Mon\u2013Fri at 06:30 \u00b7 Next: tomorrow 06:30');
  await set('11:00', []);
  assert.strictEqual(await foot(), 'Once at 11:00 \u00b7 Next: today 11:00');
  await set('09:00', []);
  assert.strictEqual(await foot(), 'Once at 09:00 \u00b7 Next: tomorrow 09:00');
  await set('08:00', ['Su']);
  assert.strictEqual(await foot(), 'Sun at 08:00 \u00b7 Next: Sun 08:00');
  await set('08:00', ['Sa', 'Su']);
  assert.strictEqual(await foot(), 'Weekends at 08:00 \u00b7 Next: Sat 08:00');
  // disabled: no promise of a next ring
  await page.locator('.al-card .al-switch').click();
  assert.strictEqual(await foot(), 'Weekends at 08:00 \u00b7 disabled');
  await page.locator('.al-card .al-switch').click();
  // cron keeps its plain "Next:" line in the same footer slot; disabled reads "Disabled"
  await page.click('.al-seg[data-v="1"]');
  await page.fill('.al-cronin', '*/20 9-17 * * 1-5');
  assert.strictEqual(await foot(), 'Next: today 10:20');
  await page.locator('.al-card .al-switch').click();
  assert.strictEqual(await foot(), 'Disabled');
});

test('weekday chips stay on one row inside the Schedule panel at narrow phone widths', async () => {
  for (const width of [360, 340]) {
    const phone = loadPhone();
    const html = openConfigPage(phone);
    const page = await browser.newPage({ viewport: { width, height: 800 } });
    await page.setContent(html);
    await heading(page, 'Alarms').click();
    await page.click('.al-add');
    const ys = await page.locator('.al-days .al-chip').evaluateAll((els) => els.map((e) => Math.round(e.getBoundingClientRect().top)));
    assert.strictEqual(new Set(ys).size, 1, `one row at ${width}px: ${ys}`);
    const w = await page.locator('.al-days .al-chip').first().evaluate((e) => e.getBoundingClientRect().width);
    assert.ok(w >= 30, `chips stay tappable at ${width}px (${Math.round(w)}px wide)`);
    await page.close();
  }
});

test('the on/off switch and the option chips keep their whole tap area (no reliance on the inset shorthand)', async () => {
  const { page } = await open();
  await heading(page, 'Alarms').click();
  await page.click('.al-add');
  const covers = async (sel) => page.locator(sel).first().evaluate((input) => {
    const box = input.getBoundingClientRect();
    const parent = input.parentElement.getBoundingClientRect();
    return { dx: Math.abs(box.left - parent.left), dy: Math.abs(box.top - parent.top),
             dw: Math.abs(box.width - parent.width), dh: Math.abs(box.height - parent.height) };
  });
  for (const sel of ['.al-card .al-switch input', '.al-card .al-chip input']) {
    const c = await covers(sel);
    assert.ok(c.dx <= 1 && c.dy <= 1 && c.dw <= 1 && c.dh <= 1, `${sel} must cover its whole control: ${JSON.stringify(c)}`);
  }
  // and the covering input really is what a tap hits
  const hit = await page.locator('.al-card .al-chip').first().evaluate((chip) => {
    chip.scrollIntoView({ block: 'center' });   // elementFromPoint only sees what is inside the viewport
    const b = chip.getBoundingClientRect();
    const el = document.elementFromPoint(b.left + b.width / 2, b.top + b.height / 2);
    return el && el.tagName;
  });
  assert.strictEqual(hit, 'INPUT');
});
