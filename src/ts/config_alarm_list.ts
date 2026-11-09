/* Clay custom component "alarmList": a variable-length list (max 16) of
   alarms, one card each. A card is collapsed to a one-line summary (time/label,
   schedule, on/off switch); tapping it expands the editor. Each alarm is either
   Basic (time + weekday chips) or Cron (one text field holding the whole
   6-field pattern "min hour dom month dow week"; short patterns are padded with
   "*", and a live "Next: ..." preview shows when it would ring). Value is an
   array of AlarmRec-shaped objects (see list_config.ts).

   Like config_tune_list.ts this is toSource()'d into the config webview, so
   everything must be self-contained (no module-scope refs at runtime). Never
   write a dollar sign next to a quote/backtick/ampersand anywhere in here, even
   in a comment or string: Clay injects this source with String.replace(), which
   reads those as special replacement patterns and corrupts the page. The cron
   parsing/next-occurrence code duplicates list_config.ts (cronNext etc.), which
   is the unit-tested, authoritative copy and what gates Save. The Tune select
   reads the Tunes list live from the sibling "TuneList" component, the Vibration
   select likewise reads "VibeList", and the
   weekday order follows the "FirstDayOfWeek" setting. The watch's full cron help
   (config.help, from config_clay.ts) sits in a collapsible item under the
   Basic/Cron selector, shown in Cron mode. */

function alarmListInitialize(this: any, _minified: any, clayConfig: any): void {
  const self = this;
  const root: HTMLElement = self.$element[0];
  const MAX = 16;   // MAX_ALARMS
  const DAY_NAMES = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const DAY_SHORT = ['Su', 'Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa'];
  const expanded: any = {};   // alarm id -> true while its card is open
  const helpOpen: any = {};   // alarm id -> true while its cron help is open

  // The watch's long cron help: paragraphs split on blank lines (the example
  // blocks keep their own line breaks, shown as-is).
  function helpEsc(s: string): string {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }
  const cronHelpHtml = (function(): string {
    const cfg: any = self.config || {};
    const paras = ('' + (cfg.help || '')).split(/\n\n+/);
    let html = '';
    for (let i = 0; i < paras.length; i++) {
      if (paras[i].replace(/^ +| +$/g, '') !== '') { html += '<p>' + helpEsc(paras[i]) + '</p>'; }
    }
    return html;
  })();

  function genId(): number {
    return 0x10000 + Math.floor(Math.random() * 0x3fff0000);
  }
  function esc(s: any): string {
    return ('' + s).replace(/&/g, '&amp;').replace(/"/g, '&quot;')
      .replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }
  function num(v: any, lo: number, hi: number, fb: number): number {
    const n = parseInt(v, 10);
    if (isNaN(n)) { return fb; }
    return n < lo ? lo : n > hi ? hi : n;
  }
  function pad2(n: number): string { return (n < 10 ? '0' : '') + n; }
  function q(r: Element, sel: string): any { return r.querySelector(sel); }

  // Weekday order follows the FirstDayOfWeek setting (1 = Monday, 0 = Sunday).
  function settingValue(key: string, fb: string): string {
    try {
      const it = clayConfig.getItemByMessageKey(key);
      const v = it ? it.get() : fb;
      return v === undefined || v === null ? fb : '' + v;
    } catch (e) { return fb; }
  }
  // Seed for a newly added alarm: the "New alarm defaults" section's values
  // (snooze, vibration/sound/increasing); everything else uses the usual
  // blank-alarm defaults in normalize().
  function newAlarmSeed(): any {
    let d: any = null;
    try {
      const it = clayConfig.getItemByMessageKey('AlarmDefaults');
      d = it ? it.get() : null;
    } catch (e) { d = null; }
    d = d || {};
    return {
      snoozeMinutes: d.snoozeMinutes,
      snoozeMax: d.snoozeMax,
      vibration: d.vibration,
      sound: d.sound,
      increasingVolume: d.increasing,
      vibePreset: d.vibePattern,
    };
  }
  function dayOrder(): number[] {
    return settingValue('FirstDayOfWeek', '1') === '0' ? [0, 1, 2, 3, 4, 5, 6] : [1, 2, 3, 4, 5, 6, 0];
  }

  // ---- cron: parsing, validation, next occurrence ----
  // Returns a { value: true } mask, or null if the field is malformed.
  function parseField(text: string, lo: number, hi: number): any {
    if (!text || text.length > 27) { return null; }
    const mask: any = {};
    const parts = text.split(',');
    for (let i = 0; i < parts.length; i++) {
      const m = /^(\*|(\d+)(?:-(\d+))?)(?:\/(\d+))?$/.exec(parts[i]);
      if (!m) { return null; }
      const step = m[4] !== undefined ? parseInt(m[4], 10) : 1;
      if (step < 1) { return null; }
      let a: number, b: number;
      if (m[1] === '*') { a = lo; b = hi; } else {
        a = parseInt(m[2], 10);
        b = m[3] !== undefined ? parseInt(m[3], 10) : a;
        if (a < lo || b > hi || a > b) { return null; }
      }
      for (let v = a; v <= b; v += step) { mask[v] = true; }
    }
    return mask;
  }
  const RANGES = [[0, 59], [0, 23], [1, 31], [1, 12], [0, 6], [1, 53]];
  // Up to 6 fields (min hour dom month dow week), padded with "*"; null if any is invalid.
  function parseCron(text: string): any[] | null {
    const f = ('' + text).trim().split(/\s+/);
    if (f.length === 0 || f[0] === '' || f.length > 6) { return null; }
    while (f.length < 6) { f.push('*'); }
    const out: any[] = [];
    for (let i = 0; i < 6; i++) {
      const m = parseField(f[i], RANGES[i][0], RANGES[i][1]);
      if (m === null) { return null; }
      out.push(m);
    }
    return out;
  }
  function isoWeek(d: Date): number {
    const t = new Date(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()));
    const dn = t.getUTCDay() || 7;
    t.setUTCDate(t.getUTCDate() + 4 - dn);
    const y0 = new Date(Date.UTC(t.getUTCFullYear(), 0, 1));
    return Math.ceil(((t.getTime() - y0.getTime()) / 86400000 + 1) / 7);
  }
  // First matching minute strictly after `now`, searching 400 days ahead
  // (the same horizon the watch uses), or null.
  function nextCron(masks: any[], now: Date): Date | null {
    for (let d = 0; d <= 400; d++) {
      const day = new Date(now.getFullYear(), now.getMonth(), now.getDate() + d);
      if (!masks[3][day.getMonth() + 1] || !masks[2][day.getDate()] ||
          !masks[4][day.getDay()] || !masks[5][isoWeek(day)]) { continue; }
      for (let h = 0; h < 24; h++) {
        if (!masks[1][h]) { continue; }
        for (let m = 0; m < 60; m++) {
          if (!masks[0][m]) { continue; }
          const c = new Date(day.getFullYear(), day.getMonth(), day.getDate(), h, m);
          if (c.getTime() > now.getTime()) { return c; }
        }
      }
    }
    return null;
  }
  // Next time a Basic alarm rings: its time on the first matching weekday
  // (any day when none is ticked = "once": today if still ahead, else tomorrow),
  // strictly after `now`. days is the bitmask, bit 0 = Sunday.
  function nextBasic(hour: number, minute: number, days: number, now: Date): Date | null {
    for (let d = 0; d <= 7; d++) {
      const day = new Date(now.getFullYear(), now.getMonth(), now.getDate() + d);
      if (days !== 0 && ((days >> day.getDay()) & 1) === 0) { continue; }
      const c = new Date(day.getFullYear(), day.getMonth(), day.getDate(), hour, minute);
      if (c.getTime() > now.getTime()) { return c; }
    }
    return null;
  }
  function fmtNext(d: Date, now: Date): string {
    const a = new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime();
    const b = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
    const diff = Math.round((a - b) / 86400000);
    const hm = pad2(d.getHours()) + ':' + pad2(d.getMinutes());
    if (diff === 0) { return 'today ' + hm; }
    if (diff === 1) { return 'tomorrow ' + hm; }
    // Same three tiers as the watch's bottom bar: today/tomorrow, weekday up to 7 days out, then a date.
    if (diff <= 7) { return DAY_NAMES[d.getDay()] + ' ' + hm; }
    // Date order follows the DateFormat setting: 0 = day.month., 1 = month/day.
    return (settingValue('DateFormat', '0') === '1'
      ? (d.getMonth() + 1) + '/' + d.getDate()
      : d.getDate() + '.' + (d.getMonth() + 1) + '.') + ' ' + hm;
  }

  // ---- tunes ----
  // Same grammar as config_tune_list.ts / list_config.ts (regex literals on
  // purpose, see the note at the top of this file).
  function validTune(text: string): boolean {
    const trimmed = ('' + text).replace(/^ +| +$/g, '');   // the watch splits on spaces only
    if (trimmed.length > 160) { return false; }   // the watch's keyboard cap; it rejects longer ones
    const toks = trimmed.split(/ +/);
    if (toks.length === 0 || toks[0] === '' || toks.length > 32) { return false; }
    const note = /^[\^_]?[C-GAB](?:'|,{1,8})?(?:[1-8](?:\/[1-8])?|\/[1-8])?$/;
    const rest = /^z(?:[1-8](?:\/[1-8])?|\/[1-8])?$/;
    for (let i = 0; i < toks.length; i++) {
      if (!note.test(toks[i]) && !rest.test(toks[i])) { return false; }
    }
    return true;
  }
  // Only tunes Save will actually send (named, valid pattern): offering any
  // other would silently fall back to the default tune.
  function tunes(): any[] {
    try {
      const it = clayConfig.getItemByMessageKey('TuneList');
      const v = it ? it.get() : [];
      const out: any[] = [];
      for (let i = 0; i < v.length; i++) {
        if (v[i] && v[i].name && ('' + v[i].name).trim() !== '' && validTune(v[i].pattern)) { out.push(v[i]); }
      }
      return out;
    } catch (e) { return []; }
  }
  function tuneOptions(sel: number): string {
    const list = tunes();
    let html = '<option value="0"' + (sel === 0 ? ' selected' : '') + '>Default tune</option>';
    let found = sel === 0;
    for (let i = 0; i < list.length; i++) {
      const on = list[i].id === sel;
      if (on) { found = true; }
      html += '<option value="' + list[i].id + '"' + (on ? ' selected' : '') + '>' + esc(list[i].name) + '</option>';
    }
    if (!found) { html += '<option value="' + sel + '" selected>(missing tune)</option>'; }
    return html;
  }

  // ---- vibrations ----
  // Same grammar as config_vibe_list.ts / list_config.ts validVibePattern.
  function validVibe(text: string): boolean {
    const toks = ('' + text).replace(/^ +| +$/g, '').split(/ +/);
    if (toks.length === 0 || toks[0] === '' || toks.length > 8) { return false; }
    const on = /^(?:[1-8](?:\/[1-8])?|\/[1-8])$/;
    const rest = /^z(?:[1-8](?:\/[1-8])?|\/[1-8])?$/;
    for (let i = 0; i < toks.length; i++) {
      if (!on.test(toks[i]) && !rest.test(toks[i])) { return false; }
    }
    return true;
  }
  function vibes(): any[] {
    try {
      const it = clayConfig.getItemByMessageKey('VibeList');
      const v = it ? it.get() : [];
      const out: any[] = [];
      for (let i = 0; i < v.length; i++) {
        if (v[i] && v[i].name && ('' + v[i].name).trim() !== '' && validVibe(v[i].pattern)) { out.push(v[i]); }
      }
      return out;
    } catch (e) { return []; }
  }
  // Option values: "p0".."p2" = Double/Short/Long preset, "v<id>" = library vibration.
  function vibeOptions(vibeId: number, preset: number): string {
    const names = ['Double', 'Short', 'Long'];
    let html = '';
    for (let i = 0; i < 3; i++) {
      html += '<option value="p' + i + '"' + (vibeId === 0 && preset === i ? ' selected' : '') + '>' + names[i] + '</option>';
    }
    const list = vibes();
    let found = vibeId === 0;
    for (let i = 0; i < list.length; i++) {
      const on = list[i].id === vibeId;
      if (on) { found = true; }
      html += '<option value="v' + list[i].id + '"' + (on ? ' selected' : '') + '>' + esc(list[i].name) + '</option>';
    }
    if (!found) { html += '<option value="v' + vibeId + '" selected>(missing vibration)</option>'; }
    return html;
  }
  // Current selection of a vibration <select>: { vibeId, vibePreset }.
  function readVibe(sel: HTMLSelectElement): any {
    const v = sel.value || 'p0';
    const last = num(sel.getAttribute('data-preset'), 0, 2, 0);
    if (v.charAt(0) === 'v') { return { vibeId: num(v.slice(1), 0, 0x7fffffff, 0), vibePreset: last }; }
    return { vibeId: 0, vibePreset: num(v.slice(1), 0, 2, 0) };
  }

  // ---- markup ----
  function opt(val: number, label: string, sel: number): string {
    return '<option value="' + val + '"' + (val === sel ? ' selected' : '') + '>' + label + '</option>';
  }
  function chip(cls: string, label: string, checked: boolean, extra?: string): string {
    return '<label class="al-chip"><input type="checkbox" class="' + cls + '"' + (extra || '') +
      (checked ? ' checked' : '') + '><span>' + label + '</span></label>';
  }

  function rowHtml(a: any): string {
    const isCron = !!a.isCron;
    let days = '';
    const order = dayOrder();
    for (let i = 0; i < 7; i++) {
      const d = order[i];
      days += chip('al-d', DAY_SHORT[d], ((a.repeatDays >> d) & 1) === 1, ' data-d="' + d + '"');
    }
    return '<div class="al-card' + (expanded[a.id] ? ' open' : '') + '" data-id="' + a.id + '">' +
      // -- collapsed summary --
      '<div class="al-sum" role="button" tabindex="0">' +
        '<div class="al-sum-text"><div class="al-sum-main"></div><div class="al-sum-sub"></div></div>' +
        '<label class="al-switch" title="Enabled"><input type="checkbox" class="al-en"' + (a.enabled ? ' checked' : '') +
          '><span class="al-slider"></span></label>' +
        '<span class="al-chev">&#9656;</span>' +
      '</div>' +
      // -- editor --
      '<div class="al-body">' +
        // the label field (its placeholder says what it is) with the DELETE button to its right
        '<div class="al-field">' +
          '<input class="al-name" type="text" maxlength="31" placeholder="Label" value="' + esc(a.name) + '">' +
          '<button type="button" class="al-del">DELETE</button></div>' +
        // Schedule: everything that decides WHEN the alarm fires, in one panel
        '<div class="al-group al-g-sched"><div class="al-gcap">Schedule</div>' +
          '<div class="al-seg-wrap" role="radiogroup">' +
            '<button type="button" class="al-seg' + (isCron ? '' : ' on') + '" data-v="0" role="radio">Basic</button>' +
            '<button type="button" class="al-seg' + (isCron ? ' on' : '') + '" data-v="1" role="radio">Cron</button></div>' +
          '<details class="al-cronhelpbox"' + (helpOpen[a.id] ? ' open' : '') + '><summary>Cron help</summary>' +
            '<div class="al-cronhelptext">' + cronHelpHtml + '</div></details>' +
          '<div class="al-basic">' +
            '<input class="al-time" type="time" value="' + pad2(a.hour) + ':' + pad2(a.minute) + '">' +
            '<div class="al-chips al-days">' + days + '</div>' +
          '</div>' +
          '<div class="al-cron">' +
            '<input class="al-cronin" type="text" spellcheck="false" placeholder="*/20 9-17 * * 1-5" value="' + esc(a.cron) + '">' +
            '<div class="al-err"></div>' +
          '</div>' +
          // the schedule in words, plus when it next rings
          '<div class="al-next"></div>' +
        '</div>' +
        // Signal: HOW it rings
        '<div class="al-group al-g-signal"><div class="al-gcap">Signal</div>' +
          '<div class="al-chips al-opts">' +
            chip('al-vib', 'Vibration', !!a.vibration) + chip('al-snd', 'Sound', !!a.sound) +
            chip('al-inc', 'Increasing', !!a.increasingVolume) +
          '</div>' +
          '<div class="al-grid">' +
            '<span class="al-lbl">Vibration</span><div><select class="al-vibsel" data-preset="' + a.vibePreset + '">' +
              vibeOptions(a.vibeId, a.vibePreset) + '</select></div>' +
            '<span class="al-lbl">Tune</span><div><select class="al-tune">' + tuneOptions(a.tuneId) + '</select></div>' +
          '</div>' +
        '</div>' +
        // Snooze and dismiss: what happens after it rings
        '<div class="al-group al-g-snooze"><div class="al-gcap">Snooze and dismiss</div>' +
          '<div class="al-grid">' +
            '<span class="al-lbl">Snooze (min)</span><div><input class="al-sm" type="number" min="0" max="120" value="' +
              a.snoozeMinutes + '"><span class="al-note al-sm-note"></span></div>' +
            '<span class="al-lbl">Max snoozes</span><div><input class="al-sx" type="number" min="0" max="20" value="' +
              a.snoozeMax + '"><span class="al-note al-sx-note"></span></div>' +
            '<span class="al-lbl">Auto-dismiss</span><div><select class="al-ad">' +
              opt(0, 'Off', a.autoDismiss) + opt(1, 'On', a.autoDismiss) + opt(2, 'On, hide', a.autoDismiss) + '</select></div>' +
          '</div>' +
        '</div>' +
      '</div>' +
    '</div>';
  }

  function normalize(e: any): any {
    e = e || {};
    return {
      id: parseInt(e.id, 10) || genId(),
      name: typeof e.name === 'string' ? e.name : '',
      enabled: e.enabled !== false,
      isCron: !!e.isCron,
      hour: num(e.hour, 0, 23, 7),
      minute: num(e.minute, 0, 59, 0),
      repeatDays: num(e.repeatDays, 0, 127, 0),
      cron: typeof e.cron === 'string' ? e.cron : '0 7 * * *',
      snoozeMinutes: num(e.snoozeMinutes, 0, 120, 10),
      snoozeMax: num(e.snoozeMax, 0, 20, 3),
      vibration: e.vibration !== false,
      sound: e.sound !== false,
      tuneId: num(e.tuneId, 0, 0x7fffffff, 0),
      autoDismiss: num(e.autoDismiss, 0, 2, 0),
      increasingVolume: !!e.increasingVolume,
      vibeId: num(e.vibeId, 0, 0x7fffffff, 0),
      vibePreset: num(e.vibePreset, 0, 2, 0),
    };
  }

  function isCronRow(r: Element): boolean {
    const on = q(r, '.al-seg.on');
    return !!on && on.getAttribute('data-v') === '1';
  }

  function currentValue(): any[] {
    const out: any[] = [];
    const rows = root.querySelectorAll('.al-card');
    for (let i = 0; i < rows.length; i++) {
      const r = rows[i] as HTMLElement;
      const t = (q(r, '.al-time').value || '07:00').split(':');
      let days = 0;
      const ds = r.querySelectorAll('.al-d');
      for (let d = 0; d < ds.length; d++) {
        if ((ds[d] as HTMLInputElement).checked) { days |= 1 << parseInt((ds[d] as HTMLElement).getAttribute('data-d') || '0', 10); }
      }
      const vib = readVibe(q(r, '.al-vibsel'));
      out.push({
        id: parseInt(r.getAttribute('data-id') || '', 10) || genId(),
        name: q(r, '.al-name').value,
        enabled: q(r, '.al-en').checked,
        isCron: isCronRow(r),
        hour: num(t[0], 0, 23, 7),
        minute: num(t[1], 0, 59, 0),
        repeatDays: days,
        cron: q(r, '.al-cronin').value,
        snoozeMinutes: num(q(r, '.al-sm').value, 0, 120, 10),
        snoozeMax: num(q(r, '.al-sx').value, 0, 20, 3),
        vibration: q(r, '.al-vib').checked,
        sound: q(r, '.al-snd').checked,
        tuneId: num(q(r, '.al-tune').value, 0, 0x7fffffff, 0),
        autoDismiss: num(q(r, '.al-ad').value, 0, 2, 0),
        increasingVolume: q(r, '.al-inc').checked,
        vibeId: vib.vibeId,
        vibePreset: vib.vibePreset,
      });
    }
    return out;
  }

  function daysSummary(mask: number): string {
    if (mask === 0) { return 'Once'; }
    if (mask === 127) { return 'Every day'; }
    if (mask === 62) { return 'Mon–Fri'; }
    if (mask === 65) { return 'Weekends'; }
    const order = dayOrder();
    const names: string[] = [];
    for (let i = 0; i < 7; i++) { if ((mask >> order[i]) & 1) { names.push(DAY_NAMES[order[i]]); } }
    return names.join(', ');
  }

  // Everything derived from a card's current inputs: summary text, which
  // editor block shows, the cron preview/error, the snooze notes.
  function refreshRow(r: HTMLElement): void {
    const isCron = isCronRow(r);
    q(r, '.al-basic').style.display = isCron ? 'none' : '';
    q(r, '.al-cron').style.display = isCron ? '' : 'none';
    q(r, '.al-cronhelpbox').style.display = isCron ? '' : 'none';
    const segs = r.querySelectorAll('.al-seg');
    for (let i = 0; i < segs.length; i++) {
      const on = (segs[i] as HTMLElement).getAttribute('data-v') === (isCron ? '1' : '0');
      (segs[i] as HTMLElement).classList.toggle('on', on);
      (segs[i] as HTMLElement).setAttribute('aria-checked', on ? 'true' : 'false');
    }

    const label = ('' + q(r, '.al-name').value).trim();
    const cronText = q(r, '.al-cronin').value;
    const masks = isCron ? parseCron(cronText) : null;
    let main: string, sub: string;
    if (isCron) {
      main = (label || 'Cron alarm');
      sub = cronText.trim() === '' ? 'No pattern' : cronText.trim();
    } else {
      const t = q(r, '.al-time').value || '07:00';
      main = t + (label ? ' · ' + label : '');
      let days = 0;
      const ds = r.querySelectorAll('.al-d');
      for (let d = 0; d < ds.length; d++) {
        if ((ds[d] as HTMLInputElement).checked) { days |= 1 << parseInt((ds[d] as HTMLElement).getAttribute('data-d') || '0', 10); }
      }
      sub = daysSummary(days);
    }
    q(r, '.al-sum-main').textContent = main;
    q(r, '.al-sum-sub').textContent = sub;
    r.classList.toggle('off', !q(r, '.al-en').checked);

    const err = q(r, '.al-err');
    const next = q(r, '.al-next');
    const on = q(r, '.al-en').checked;
    err.textContent = '';
    next.textContent = '';
    const now = new Date();
    if (isCron) {
      if (masks === null) {
        err.textContent = 'Invalid cron pattern (this alarm will not be saved)';
        r.classList.add('bad');
      } else {
        r.classList.remove('bad');
        const n = nextCron(masks, now);
        next.textContent = !on ? 'Disabled' : n ? 'Next: ' + fmtNext(n, now) : 'Next: never (nothing within a year)';
      }
    } else {
      r.classList.remove('bad');
      const t = (q(r, '.al-time').value || '07:00').split(':');
      let days = 0;
      const ds = r.querySelectorAll('.al-d');
      for (let d = 0; d < ds.length; d++) {
        if ((ds[d] as HTMLInputElement).checked) { days |= 1 << parseInt((ds[d] as HTMLElement).getAttribute('data-d') || '0', 10); }
      }
      const h = num(t[0], 0, 23, 7), m = num(t[1], 0, 59, 0);
      const words = daysSummary(days) + ' at ' + pad2(h) + ':' + pad2(m);
      const n = nextBasic(h, m, days, now);
      next.textContent = !on ? words + ' \u00b7 disabled' : n ? words + ' \u00b7 Next: ' + fmtNext(n, now) : words;
    }
    const sm = parseInt(q(r, '.al-sm').value, 10);
    q(r, '.al-sm-note').textContent = sm === 0 ? 'Off' : '';
    const sx = parseInt(q(r, '.al-sx').value, 10);
    q(r, '.al-sx-note').textContent = sx === 0 ? 'unlimited' : '';
  }

  function refreshAll(): void {
    const rows = root.querySelectorAll('.al-card');
    for (let i = 0; i < rows.length; i++) { refreshRow(rows[i] as HTMLElement); }
  }

  function updateAdd(): void {
    const add = root.querySelector('.al-add') as HTMLButtonElement;
    if (add) { add.style.display = (root.querySelectorAll('.al-card').length >= MAX) ? 'none' : ''; }
  }

  // Same order as the watch's main list (rebuild_order in main.c): ascending
  // by raw clock time of day, ties keep their current order. A cron alarm sorts
  // by its earliest matching hour and minute (cron_sort_key on the watch).
  function sortKey(a: any): number {
    if (!a.isCron) { return a.hour * 60 + a.minute; }
    const masks = parseCron(a.cron);
    if (masks === null) { return 24 * 60; }   // invalid cron (won't be saved) goes last
    let hour = 0, minute = 0, v: any;
    for (v = 0; v < 24; v++) { if (masks[1][v]) { hour = v; break; } }
    for (v = 0; v < 60; v++) { if (masks[0][v]) { minute = v; break; } }
    return hour * 60 + minute;
  }
  function sortedByTime(list: any[]): any[] {
    const tagged: any[] = [];
    for (let i = 0; i < list.length; i++) {
      const n = normalize(list[i]);
      tagged.push({ a: n, k: sortKey(n), i: i });
    }
    tagged.sort(function(x: any, y: any) { return x.k !== y.k ? x.k - y.k : x.i - y.i; });
    return tagged.map(function(t: any) { return t.a; });
  }

  // `sort` re-orders the cards by time. It happens when a list is loaded and
  // when a card is collapsed -- not while typing, which would make the card
  // under the user's finger jump away; a freshly added alarm therefore stays
  // at the bottom until it's collapsed.
  function rebuild(list: any[], sort?: boolean): void {
    if (sort) { list = sortedByTime(list); }
    const listEl = root.querySelector('.al-list') as HTMLElement;
    let html = '';
    for (let i = 0; i < list.length && i < MAX; i++) { html += rowHtml(normalize(list[i])); }
    listEl.innerHTML = html;
    refreshAll();
    updateAdd();
  }

  self._alCurrentValue = currentValue;
  self._alRebuild = rebuild;

  function cardOf(t: Element | null): HTMLElement | null {
    return t && t.closest ? (t.closest('.al-card') as HTMLElement) : null;
  }
  function setOpen(card: HTMLElement, open: boolean): void {
    card.classList.toggle('open', open);
    const id = card.getAttribute('data-id') || '';
    if (open) { expanded[id] = true; } else { delete expanded[id]; }
  }

  root.addEventListener('input', function(ev: Event) {
    const r = cardOf(ev.target as Element);
    if (r) { refreshRow(r); }
  });
  // Remember which cards have their cron help open, across re-renders
  // (sorting, deleting another card, ...). `toggle` doesn't bubble: capture it.
  root.addEventListener('toggle', function(ev: Event) {
    const d = ev.target as HTMLDetailsElement;
    const card = cardOf(d);
    if (!card || !d.classList || !d.classList.contains('al-cronhelpbox')) { return; }
    const id = card.getAttribute('data-id') || '';
    if (d.open) { helpOpen[id] = true; } else { delete helpOpen[id]; }
  }, true);
  root.addEventListener('change', function(ev: Event) {
    const t = ev.target as HTMLElement;
    if (t && t.classList && t.classList.contains('al-vibsel')) {
      // Remember the last chosen preset so picking a library vibration and
      // later a preset again still round-trips cleanly.
      const sel = t as HTMLSelectElement;
      if (sel.value.charAt(0) === 'p') { sel.setAttribute('data-preset', sel.value.slice(1)); }
    }
    const r = cardOf(t);
    if (r) { refreshRow(r); }
  });
  // Tune options track the live Tunes list: refresh them whenever the user
  // reaches for a tune dropdown (keeps the current selection by id).
  root.addEventListener('mousedown', function(ev: Event) {
    const t = ev.target as HTMLElement;
    if (t && t.classList && t.classList.contains('al-tune')) {
      const sel = t as HTMLSelectElement;
      sel.innerHTML = tuneOptions(num(sel.value, 0, 0x7fffffff, 0));
    }
    if (t && t.classList && t.classList.contains('al-vibsel')) {
      refreshVibeSelect(t as HTMLSelectElement);
    }
  });
  function refreshVibeSelect(sel: HTMLSelectElement): void {
    const cur = readVibe(sel);
    sel.innerHTML = vibeOptions(cur.vibeId, cur.vibePreset);
  }
  function refreshAllVibes(): void {
    const sels = root.querySelectorAll('.al-vibsel');
    for (let i = 0; i < sels.length; i++) { refreshVibeSelect(sels[i] as HTMLSelectElement); }
  }
  function refreshAllTunes(): void {
    const sels = root.querySelectorAll('.al-tune');
    for (let i = 0; i < sels.length; i++) {
      const s = sels[i] as HTMLSelectElement;
      s.innerHTML = tuneOptions(num(s.value, 0, 0x7fffffff, 0));
    }
  }
  // Settings this list depends on (weekday order, date order): re-render
  // rows in place when they change, keeping every card's values/open state.
  function rerender(): void { rebuild(currentValue()); }
  // The Tunes/settings components may not exist yet when this list is first
  // rendered (saved values are applied before AFTER_BUILD), so re-derive once
  // built, and again whenever they change.
  if (clayConfig && clayConfig.on && clayConfig.EVENTS) {
    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
      rerender();
      try { clayConfig.getItemByMessageKey('TuneList').on('change', refreshAllTunes); } catch (e) { /* ignore */ }
      try { clayConfig.getItemByMessageKey('VibeList').on('change', refreshAllVibes); } catch (e) { /* ignore */ }
      try { clayConfig.getItemByMessageKey('FirstDayOfWeek').on('change', rerender); } catch (e) { /* ignore */ }
      try { clayConfig.getItemByMessageKey('DateFormat').on('change', refreshAll); } catch (e) { /* ignore */ }
    });
  }

  function toggleFromSummary(ev: Event): void {
    const t = ev.target as HTMLElement;
    if (t.closest && t.closest('.al-switch')) { return; }   // the on/off switch must not expand the card
    const card = cardOf(t);
    if (!card) { return; }
    const wasOpen = card.classList.contains('open');
    setOpen(card, !wasOpen);
    if (wasOpen) { rebuild(currentValue(), true); }   // done editing: settle into time order
  }
  root.addEventListener('keydown', function(ev: Event) {
    const k = (ev as KeyboardEvent).key;
    const t = ev.target as HTMLElement;
    if ((k === 'Enter' || k === ' ') && t.classList && t.classList.contains('al-sum')) {
      ev.preventDefault();
      toggleFromSummary(ev);
    }
  });

  root.addEventListener('click', function(ev: Event) {
    const raw = ev.target as HTMLElement;
    if (!raw) { return; }
    if (raw.closest && raw.closest('.al-sum')) { toggleFromSummary(ev); return; }
    const target = raw.tagName === 'BUTTON' ? raw : (raw.closest ? (raw.closest('button') as HTMLElement) : null);
    if (!target) { return; }
    if (target.classList.contains('al-add')) {
      const v = currentValue();
      if (v.length < MAX) {
        const e = normalize(newAlarmSeed());
        expanded[e.id] = true;   // a new alarm opens ready to edit
        v.push(e);
        rebuild(v);
        self.trigger('change');
      }
      return;
    }
    if (target.classList.contains('al-seg')) {
      const card = cardOf(target);
      if (card) {
        const all = card.querySelectorAll('.al-seg');
        for (let i = 0; i < all.length; i++) { (all[i] as HTMLElement).classList.remove('on'); }
        target.classList.add('on');
        refreshRow(card);
      }
      return;
    }
    if (target.classList.contains('al-del')) {
      const card = cardOf(target);
      const rows = root.querySelectorAll('.al-card');
      let idx = -1;
      for (let i = 0; i < rows.length; i++) { if (rows[i] === card) { idx = i; } }
      if (idx === -1) { return; }
      const v = currentValue();
      delete expanded[v[idx].id];
      v.splice(idx, 1);
      rebuild(v);
      self.trigger('change');
    }
  });
}

const alarmListComponent = {
  name: 'alarmList',
  template:
    '<div class="al-rootc">' +
    '<div class="al-list"></div>' +
    '<button type="button" class="al-add">+ Add alarm</button>' +
    '</div>',
  style:
    // Same inset as Clay's own components (12px at the sides and below the heading),
    // so nothing touches the section outline.
    '.al-rootc{padding:0.75rem 0.75rem 0.5rem 0.75rem;box-sizing:border-box}' +
    '.al-card{background:#444;border-radius:0.6rem;margin:0 0 0.7rem 0;overflow:hidden;border:1px solid #555}' +
    '.al-card.bad{border-color:#ff6b6b}' +
    // labelled panels (white captions): Schedule / Signal / Snooze and dismiss
    '.al-group{background:#3a3a3a;border:1px solid #5a5a5a;border-radius:0.5rem;padding:0.5rem 0.65rem 0.45rem 0.65rem;margin:0.8rem 0 0 0}' +
    '.al-gcap{color:#fff;font-size:0.72rem;font-weight:bold;text-transform:uppercase;letter-spacing:0.08em;margin:0 0 0.5rem 0}' +
    // collapsed summary row
    '.al-sum{display:flex;align-items:center;padding:0.65rem 0.8rem;cursor:pointer;gap:0.6rem}' +
    '.al-sum-text{flex:1 1 auto;min-width:0}' +
    '.al-sum-main{color:#fff;font-size:1.05rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}' +
    '.al-sum-sub{color:#bbb;font-size:0.8rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-top:2px}' +
    '.al-card.off .al-sum-main,.al-card.off .al-sum-sub{opacity:0.5}' +
    // Big triangle with a generous tap area, kept well clear of the switch
    // (a press on the chevron must never land on the on/off toggle).
    '.al-chev{flex:0 0 auto;display:flex;align-items:center;justify-content:center;width:3rem;height:3rem;' +
      'margin:-0.7rem -0.8rem -0.7rem 0.5rem;color:#ddd;font-size:1.7rem;line-height:1;transition:transform 0.15s}' +
    '.al-card.open .al-chev{transform:rotate(90deg)}' +
    // on/off switch
    '.al-switch{position:relative;display:inline-block;width:2.6rem;height:1.5rem;flex:0 0 auto;margin:0}' +
    '.al-switch input{position:absolute;opacity:0;top:0;left:0;width:100%;height:100%;margin:0;cursor:pointer;z-index:1}' +
    '.al-slider{position:absolute;top:0;right:0;bottom:0;left:0;background:#777;border-radius:1.5rem;transition:background 0.15s}' +
    '.al-slider:before{content:"";position:absolute;width:1.1rem;height:1.1rem;left:0.2rem;top:0.2rem;background:#fff;border-radius:50%;transition:transform 0.15s}' +
    '.al-switch input:checked + .al-slider{background:#ff4700}' +
    '.al-switch input:checked + .al-slider:before{transform:translateX(1.1rem)}' +
    // editor
    '.al-body{display:none;padding:0.2rem 0.8rem 0.8rem 0.8rem;border-top:1px solid #555}' +
    '.al-card.open .al-body{display:block}' +
    '.al-lbl{color:#ddd;font-size:0.9rem}' +
    '.al-field{display:flex;align-items:center;gap:0.6rem;margin:0.7rem 0}' +
    '.al-name,.al-time,.al-cronin,.al-tune,.al-vibsel,.al-ad,.al-sm,.al-sx{box-sizing:border-box;height:2.5rem;margin:0;' +
      'background-color:#666;color:#fff;border:none;border-radius:0.3rem;padding:0 0.6rem;color-scheme:dark;font-size:1rem}' +
    '.al-name{flex:1 1 auto;min-width:0}' +
    // segmented Basic/Cron switch
    '.al-seg-wrap{display:flex;margin:0 0 0.7rem 0}' +
    '.al-seg{flex:1 1 50%;min-width:0;margin:0;height:2.3rem;padding:0;background:#666;color:#ddd;border:none;border-radius:0}' +
    '.al-seg:first-child{border-radius:0.3rem 0 0 0.3rem}.al-seg:last-child{border-radius:0 0.3rem 0.3rem 0}' +
    '.al-seg.on{background:#ff4700;color:#fff}' +
    // chips (weekdays, signal toggles)
    '.al-chips{display:flex;flex-wrap:wrap;gap:0.4rem;margin:0.6rem 0}' +
    '.al-chip{position:relative;display:inline-block;margin:0;padding:0}' +
    '.al-chip input{position:absolute;opacity:0;top:0;left:0;width:100%;height:100%;margin:0;cursor:pointer}' +
    '.al-chip span{display:inline-block;padding:0.4rem 0.7rem;border-radius:999px;background:#666;color:#ddd;font-size:0.9rem;min-width:1.6rem;text-align:center}' +
    // Seven weekday chips share one row at any width.
    '.al-days{flex-wrap:nowrap;gap:0.3rem}' +
    '.al-days .al-chip{flex:1 1 0;min-width:0}' +
    '.al-days .al-chip span{display:block;width:100%;min-width:0;padding:0;height:2.3rem;line-height:2.3rem;border-radius:999px;box-sizing:border-box}' +
    '.al-chip input:checked + span{background:#ff4700;color:#fff}' +
    // cron block
    '.al-cronin{width:100%;font-family:monospace}' +
    // the schedule footer: the schedule in words + when it next rings
    '.al-next{color:#9fd8a0;font-size:0.85rem;margin:0.6rem 0 0 0;padding-top:0.45rem;border-top:1px dashed #666}' +
    '.al-next:empty{display:none}' +
    '.al-group .al-chips{margin:0 0 0.4rem 0}' +
    '.al-group .al-days{margin:0.6rem 0 0.2rem 0}' +
    '.al-group .al-grid{margin:0.1rem 0 0.1rem 0}' +
    '.al-err{color:#ff6b6b;font-size:0.85rem;margin:0.35rem 0 0 0}' +
    '.al-cronhelpbox{margin:0 0 0.7rem 0;background:#3c3c3c;border:1px solid #555;border-radius:0.5rem}' +
    '.al-cronhelpbox summary{cursor:pointer;padding:0.5rem 0.8rem;color:#ddd;font-size:0.9rem}' +
    '.al-cronhelptext{padding:0 0.8rem 0.5rem 0.8rem;color:#ccc;font-size:0.82rem;line-height:1.4}' +
    '.al-cronhelptext p{margin:0 0 0.6rem 0;white-space:pre-wrap}' +
    // aligned two-column block
    '.al-grid{display:grid;grid-template-columns:7rem 1fr;gap:0.55rem 0.6rem;align-items:center;margin:0.6rem 0}' +
    '.al-tune,.al-vibsel,.al-ad{width:100%}' +
    '.al-sm,.al-sx{width:5rem}' +
    '.al-note{color:#bbb;font-size:0.8rem;margin-left:0.6rem}' +
    // DELETE: a real (destructive) button to the right of the label field
    '.al-del{flex:0 0 auto;min-width:0;width:auto;margin:0;padding:0 0.9rem;height:2.5rem;background:#b3302a;color:#fff;border:1px solid #d9534f;' +
      'border-radius:0.3rem;font-size:0.9rem;font-weight:normal;cursor:pointer}' +
    '.al-del:active{background:#8f2722}' +
    '.al-add{margin:0.4rem 0 0.6rem 0}',
  manipulator: {
    get: function(this: any): any[] {
      return this._alCurrentValue ? this._alCurrentValue() : [];
    },
    set: function(this: any, value: any) {
      let list: any[] = [];
      let v: any = value;
      if (v && typeof v === 'object' && !Array.isArray(v) && v.value !== undefined) { v = v.value; }
      if (Array.isArray(v)) {
        list = v;
      } else if (typeof v === 'string' && v !== '') {
        try { const p = JSON.parse(v); if (Array.isArray(p)) { list = p; } } catch (e) { list = []; }
      }
      if (this._alRebuild) { this._alRebuild(list, true); }
      return this;
    },
  },
  defaults: { label: '' },
  initialize: alarmListInitialize,
};

export = alarmListComponent;
