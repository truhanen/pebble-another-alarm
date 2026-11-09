/* Clay custom component "alarmList": a variable-length list (max 16) of
   alarms. Each alarm is either Basic (time + weekday checkboxes) or Cron (one
   text field holding the whole 6-field pattern "min hour dom month dow week";
   shorter patterns are padded with "*"). Value is an array of AlarmRec-shaped
   objects (see list_config.ts). Modeled on pebble-another-timer's timerList.
   Like config_tune_list.ts this is toSource()'d into the config webview, so
   everything must be self-contained (no module-scope refs at runtime). The
   cron check duplicates list_config.ts's normalizeCron (authoritative copy,
   which also gates what Save sends). The tune <select> reads the Tunes list
   live from the sibling "TuneList" component. */

function alarmListInitialize(this: any, _minified: any, clayConfig: any): void {
  const self = this;
  const root: HTMLElement = self.$element[0];
  const MAX = 16;   // MAX_ALARMS
  const DAYS = ['Su', 'Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa'];

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

  // ---- cron validation (see header comment) ----
  const RANGES = [[0, 59], [0, 23], [1, 31], [1, 12], [0, 6], [1, 53]];
  function validField(text: string, lo: number, hi: number): boolean {
    if (!text || text.length > 27) { return false; }
    const parts = text.split(',');
    for (let i = 0; i < parts.length; i++) {
      const m = /^(\*|(\d+)(?:-(\d+))?)(?:\/(\d+))?$/.exec(parts[i]);
      if (!m) { return false; }
      if (m[4] !== undefined && parseInt(m[4], 10) < 1) { return false; }
      if (m[1] === '*') { continue; }
      const a = parseInt(m[2], 10);
      const b = m[3] !== undefined ? parseInt(m[3], 10) : a;
      if (a < lo || b > hi || a > b) { return false; }
    }
    return true;
  }
  function cronOk(text: string): boolean {
    const f = ('' + text).trim().split(/\s+/);
    if (f.length === 0 || f[0] === '' || f.length > 6) { return false; }
    for (let i = 0; i < f.length; i++) {
      if (!validField(f[i], RANGES[i][0], RANGES[i][1])) { return false; }
    }
    return true;
  }

  // Same grammar as config_tune_list.ts / list_config.ts (regex literals on
  // purpose, see the note in config_tune_list.ts about Clay's String.replace).
  function validTune(text: string): boolean {
    const toks = ('' + text).trim().split(/\s+/);
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

  function opt(val: number, label: string, sel: number): string {
    return '<option value="' + val + '"' + (val === sel ? ' selected' : '') + '>' + label + '</option>';
  }

  function rowHtml(a: any): string {
    const isCron = !!a.isCron;
    let days = '';
    for (let d = 0; d < 7; d++) {
      days += '<label class="al-day"><input type="checkbox" class="al-d" data-d="' + d + '"' +
        (((a.repeatDays >> d) & 1) ? ' checked' : '') + '>' + DAYS[d] + '</label>';
    }
    return '<div class="al-row" data-id="' + a.id + '">' +
      '<div class="al-line">' +
      '<input class="al-name" type="text" maxlength="31" placeholder="Label" value="' + esc(a.name) + '">' +
      '<button type="button" class="al-del" title="Remove">&#10005;</button>' +
      '</div>' +
      '<div class="al-line">' +
      '<select class="al-mode"><option value="0"' + (isCron ? '' : ' selected') + '>Basic</option>' +
      '<option value="1"' + (isCron ? ' selected' : '') + '>Cron</option></select>' +
      '<label class="al-chk"><input type="checkbox" class="al-en"' + (a.enabled ? ' checked' : '') + '> Enabled</label>' +
      '</div>' +
      '<div class="al-basic"' + (isCron ? ' style="display:none"' : '') + '>' +
      '<input class="al-time" type="time" value="' + pad2(a.hour) + ':' + pad2(a.minute) + '">' +
      '<div class="al-days">' + days + '</div>' +
      '</div>' +
      '<div class="al-cron"' + (isCron ? '' : ' style="display:none"') + '>' +
      '<input class="al-cronin" type="text" placeholder="min hour dom month dow week, e.g. */20 9-17 * * 1-5" value="' + esc(a.cron) + '">' +
      '<div class="al-err"></div>' +
      '</div>' +
      '<div class="al-line al-opts">' +
      '<label class="al-chk"><input type="checkbox" class="al-vib"' + (a.vibration ? ' checked' : '') + '> Vibration</label>' +
      '<label class="al-chk"><input type="checkbox" class="al-snd"' + (a.sound ? ' checked' : '') + '> Sound</label>' +
      '<label class="al-chk"><input type="checkbox" class="al-inc"' + (a.increasingVolume ? ' checked' : '') + '> Increasing</label>' +
      '</div>' +
      '<div class="al-line"><span>Tune</span><select class="al-tune">' + tuneOptions(a.tuneId) + '</select></div>' +
      '<div class="al-line"><span>Snooze</span>' +
      '<input class="al-sm" type="number" min="0" max="120" value="' + a.snoozeMinutes + '"><span>min (0 = off) &times;</span>' +
      '<input class="al-sx" type="number" min="0" max="20" value="' + a.snoozeMax + '"><span>(0 = &infin;)</span></div>' +
      '<div class="al-line"><span>Auto-dismiss</span><select class="al-ad">' +
      opt(0, 'Off', a.autoDismiss) + opt(1, 'On', a.autoDismiss) + opt(2, 'On, hide', a.autoDismiss) + '</select></div>' +
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
    };
  }

  function q(r: HTMLElement, sel: string): any { return r.querySelector(sel); }

  function currentValue(): any[] {
    const out: any[] = [];
    const rows = root.querySelectorAll('.al-row');
    for (let i = 0; i < rows.length; i++) {
      const r = rows[i] as HTMLElement;
      const t = (q(r, '.al-time').value || '07:00').split(':');
      let days = 0;
      const ds = r.querySelectorAll('.al-d');
      for (let d = 0; d < ds.length; d++) {
        if ((ds[d] as HTMLInputElement).checked) { days |= 1 << parseInt((ds[d] as HTMLElement).getAttribute('data-d') || '0', 10); }
      }
      out.push({
        id: parseInt(r.getAttribute('data-id') || '', 10) || genId(),
        name: q(r, '.al-name').value,
        enabled: q(r, '.al-en').checked,
        isCron: q(r, '.al-mode').value === '1',
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
      });
    }
    return out;
  }

  function refreshRow(r: HTMLElement): void {
    const isCron = q(r, '.al-mode').value === '1';
    q(r, '.al-basic').style.display = isCron ? 'none' : '';
    q(r, '.al-cron').style.display = isCron ? '' : 'none';
    const txt = q(r, '.al-cronin').value;
    q(r, '.al-err').textContent = (!isCron || cronOk(txt)) ? '' : 'Invalid cron pattern (alarm will not be saved)';
  }

  function updateAdd(): void {
    const add = root.querySelector('.al-add') as HTMLButtonElement;
    if (add) { add.style.display = (root.querySelectorAll('.al-row').length >= MAX) ? 'none' : ''; }
  }

  function rebuild(list: any[]): void {
    const listEl = root.querySelector('.al-list') as HTMLElement;
    let html = '';
    for (let i = 0; i < list.length && i < MAX; i++) { html += rowHtml(normalize(list[i])); }
    listEl.innerHTML = html;
    const rows = root.querySelectorAll('.al-row');
    for (let i = 0; i < rows.length; i++) { refreshRow(rows[i] as HTMLElement); }
    updateAdd();
  }

  self._alCurrentValue = currentValue;
  self._alRebuild = rebuild;

  function rowOf(t: HTMLElement | null): HTMLElement | null {
    return t && t.closest ? (t.closest('.al-row') as HTMLElement) : null;
  }
  root.addEventListener('input', function(ev: Event) {
    const r = rowOf(ev.target as HTMLElement);
    if (r) { refreshRow(r); }
  });
  root.addEventListener('change', function(ev: Event) {
    const r = rowOf(ev.target as HTMLElement);
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
  });
  function refreshAllTunes(): void {
    const sels = root.querySelectorAll('.al-tune');
    for (let i = 0; i < sels.length; i++) {
      const s = sels[i] as HTMLSelectElement;
      s.innerHTML = tuneOptions(num(s.value, 0, 0x7fffffff, 0));
    }
  }
  // The Tunes component may not exist yet when this list is first rendered
  // (saved values are applied before AFTER_BUILD), so re-label once built,
  // and again whenever the Tunes list changes.
  if (clayConfig && clayConfig.on && clayConfig.EVENTS) {
    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
      refreshAllTunes();
      try { clayConfig.getItemByMessageKey('TuneList').on('change', refreshAllTunes); } catch (e) { /* ignore */ }
    });
  }
  root.addEventListener('click', function(ev: Event) {
    let target = ev.target as HTMLElement;
    if (!target) { return; }
    if (target.tagName !== 'BUTTON') {
      target = target.closest ? (target.closest('button') as HTMLElement) : null as any;
    }
    if (!target) { return; }
    if (target.classList.contains('al-add')) {
      const v = currentValue();
      if (v.length < MAX) { v.push(normalize({})); rebuild(v); self.trigger('change'); }
      return;
    }
    if (target.classList.contains('al-del')) {
      const row = rowOf(target);
      const rows = root.querySelectorAll('.al-row');
      let idx = -1;
      for (let i = 0; i < rows.length; i++) { if (rows[i] === row) { idx = i; } }
      if (idx === -1) { return; }
      const v = currentValue();
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
    '<div class="al-help">Basic: time plus weekdays (none = once). Cron: min hour dom month dow week, ' +
    'space-separated; missing trailing fields mean *. Weekdays 0-6 (0 = Sunday).</div>' +
    '</div>',
  style:
    '.al-row{margin:0 0 14px 0;padding:0 0 10px 0;border-bottom:1px solid #444}' +
    '.al-line{display:flex;align-items:center;flex-wrap:wrap;margin:0 0 6px 0}' +
    '.al-line span{margin:0 6px 0 0;color:#fff}' +
    '.al-name,.al-time,.al-cronin,.al-mode,.al-tune,.al-ad,.al-sm,.al-sx{box-sizing:border-box;height:2.6rem;margin:0 6px 6px 0;' +
      'background-color:#767676;color:#fff;border:none;border-radius:0.3rem;padding:0 0.5rem;color-scheme:dark}' +
    '.al-name{flex:1 1 auto;min-width:0}' +
    '.al-cronin{width:100%;font-family:monospace}' +
    '.al-sm,.al-sx{width:4rem}' +
    '.al-chk{margin:0 12px 6px 0;color:#fff;white-space:nowrap}' +
    '.al-days{display:flex;flex-wrap:wrap;margin:0 0 6px 0}' +
    '.al-day{margin:0 8px 0 0;color:#fff}' +
    '.al-line button{flex:0 0 auto;min-width:0;width:2.6rem;height:2.6rem;margin:0 0 6px 0;padding:0}' +
    '.al-err{color:#ff6b6b;font-size:0.8rem}' +
    '.al-add{margin:8px 0 6px 0}' +
    '.al-help{font-size:0.8rem;color:#bbb;margin:4px 0 8px 0}',
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
      if (this._alRebuild) { this._alRebuild(list); }
      return this;
    },
  },
  defaults: { label: '' },
  initialize: alarmListInitialize,
};

export = alarmListComponent;
