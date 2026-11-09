/* Clay custom component "alarmDefaults": the "New alarm defaults" block, laid
   out like the signal/snooze part of an alarm card (config_alarm_list.ts) --
   same chips, same aligned grid, same "Off"/"Unlimited" notes -- and
   reusing its CSS classes (.al-chips/.al-chip/.al-grid/.al-lbl/.al-note, defined
   in that component's style; both are always on the page together).
   Value: { snoozeMinutes, snoozeMax, vibration, sound, increasing, vibePattern }
   (see AlarmDefaults in dict.ts). toSource()'d into the config webview, so it
   must be self-contained; never write a dollar sign next to a quote/backtick/
   ampersand here, even in a comment (Clay injects this source via
   String.replace, which would read it as a replacement pattern). */

function alarmDefaultsInitialize(this: any, _minified: any, clayConfig: any): void {
  const self = this;
  const root: HTMLElement = self.$element[0];

  function q(sel: string): any { return root.querySelector(sel); }
  function num(v: any, lo: number, hi: number, fb: number): number {
    const n = parseInt(v, 10);
    if (isNaN(n)) { return fb; }
    return n < lo ? lo : n > hi ? hi : n;
  }
  function chip(cls: string, label: string): string {
    return '<label class="al-chip"><input type="checkbox" class="' + cls + '"><span>' + label + '</span></label>';
  }

  function notes(): void {
    q('.ad-sm-note').textContent = num(q('.ad-sm').value, 0, 120, 10) === 0 && q('.ad-sm').value !== '' ? 'Off' : '';
    q('.ad-sx-note').textContent = num(q('.ad-sx').value, 0, 20, 3) === 0 && q('.ad-sx').value !== '' ? 'Unlimited' : '';
  }

  // The Vibration select's entries: the watch's built-in presets, named in the
  // item's config (config_clay.ts), so this can't drift from the watch's list.
  function esc(s: any): string {
    return ('' + s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }
  // Library entries Save will actually send (named, valid pattern), read live
  // from the sibling TuneList / VibeList components.
  function validTune(text: string): boolean {
    const trimmed = ('' + text).replace(/^ +| +$/g, '');
    if (trimmed.length > 160) { return false; }
    const toks = trimmed.split(/ +/);
    if (toks.length === 0 || toks[0] === '' || toks.length > 32) { return false; }
    const note = /^(?:[\^_])?[C-GAB](?:'|,{1,8})?(?:[1-8](?:\/[1-8])?|\/[1-8])?$/;
    const rest = /^z(?:[1-8](?:\/[1-8])?|\/[1-8])?$/;
    for (let i = 0; i < toks.length; i++) { if (!note.test(toks[i]) && !rest.test(toks[i])) { return false; } }
    return true;
  }
  function validVibe(text: string): boolean {
    const toks = ('' + text).replace(/^ +| +$/g, '').split(/ +/);
    if (toks.length === 0 || toks[0] === '' || toks.length > 8) { return false; }
    const on = /^(?:[1-8](?:\/[1-8])?|\/[1-8])$/;
    const rest = /^z(?:[1-8](?:\/[1-8])?|\/[1-8])?$/;
    for (let i = 0; i < toks.length; i++) { if (!on.test(toks[i]) && !rest.test(toks[i])) { return false; } }
    return true;
  }
  function library(key: string, valid: (t: string) => boolean): any[] {
    const out: any[] = [];
    try {
      const it = clayConfig.getItemByMessageKey(key);
      const v = it ? it.get() : [];
      for (let i = 0; i < v.length; i++) {
        if (v[i] && v[i].name && ('' + v[i].name).trim() !== '' && valid(v[i].pattern)) { out.push(v[i]); }
      }
    } catch (e) { /* not built yet */ }
    return out;
  }
  // Option values: "p<index>" = built-in, "v<id>" = the user's own entry.
  function selectOptions(names: string[], order: number[], lib: any[], id: number, preset: number, missing: string): string {
    let html = '';
    for (let k = 0; k < order.length; k++) {
      const i = order[k];
      html += '<option value="p' + i + '"' + (id === 0 && preset === i ? ' selected' : '') + '>' + esc(names[i]) + '</option>';
    }
    let found = id === 0;
    for (let i = 0; i < lib.length; i++) {
      const on = lib[i].id === id;
      if (on) { found = true; }
      html += '<option value="v' + lib[i].id + '"' + (on ? ' selected' : '') + '>' + esc(lib[i].name) + '</option>';
    }
    if (!found) { html += '<option value="v' + id + '" selected>' + missing + '</option>'; }
    return html;
  }
  function range(n: number): number[] { const o: number[] = []; for (let i = 0; i < n; i++) { o.push(i); } return o; }
  function vibeSelectHtml(id: number, preset: number): string {
    const cfg: any = self.config || {};
    const names: string[] = Array.isArray(cfg.vibePresets) ? cfg.vibePresets : [];
    const order: number[] = Array.isArray(cfg.vibeOrder) ? cfg.vibeOrder : range(names.length);
    return selectOptions(names, order, library('VibeList', validVibe), id, preset, '(missing vibration)');
  }
  function tuneSelectHtml(id: number, preset: number): string {
    const cfg: any = self.config || {};
    const names: string[] = Array.isArray(cfg.soundPresets) ? cfg.soundPresets : [];
    return selectOptions(names, range(names.length), library('TuneList', validTune), id, preset, '(missing tune)');
  }
  // Current selection of a <select>: { id, preset } (a library pick keeps the last built-in).
  function readSel(sel: any): any {
    const v = sel.value || 'p0';
    const last = num(sel.getAttribute('data-preset'), 0, 255, 0);
    if (v.charAt(0) === 'v') { return { id: num(v.slice(1), 0, 0x7fffffff, 0), preset: last }; }
    const p = num(v.slice(1), 0, 255, 0);
    sel.setAttribute('data-preset', '' + p);
    return { id: 0, preset: p };
  }
  function fill(cls: string, html: string, preset: number): void {
    const sel = q(cls);
    sel.innerHTML = html;
    sel.setAttribute('data-preset', '' + preset);
  }

  function build(): void {
    q('.ad-body').innerHTML =
      '<div class="al-chips ad-opts">' + chip('ad-vib', 'Vibration') + chip('ad-snd', 'Sound') + chip('ad-inc', 'Increasing') + '</div>' +
      '<div class="al-grid">' +
        '<span class="al-lbl">Vibration</span><div><select class="ad-vp"></select></div>' +
        '<span class="al-lbl">Tune</span><div><select class="ad-tn"></select></div>' +
        '<span class="al-lbl">Snooze (min)</span><div><input class="ad-sm" type="number" min="0" max="120"><span class="al-note ad-sm-note"></span></div>' +
        '<span class="al-lbl">Max snoozes</span><div><input class="ad-sx" type="number" min="0" max="20"><span class="al-note ad-sx-note"></span></div>' +
      '</div>';
  }

  function currentValue(): any {
    const vv = readSel(q('.ad-vp'));
    const tt = readSel(q('.ad-tn'));
    return {
      snoozeMinutes: num(q('.ad-sm').value, 0, 120, 10),
      snoozeMax: num(q('.ad-sx').value, 0, 20, 3),
      vibration: q('.ad-vib').checked,
      sound: q('.ad-snd').checked,
      increasing: q('.ad-inc').checked,
      vibePattern: vv.preset,
      vibeId: vv.id,
      tuneId: tt.id,
      soundPreset: tt.preset,
    };
  }

  let cur: any = { vibeId: 0, vibePreset: 0, tuneId: 0, soundPreset: 0 };
  function refill(): void {
    fill('.ad-vp', vibeSelectHtml(cur.vibeId, cur.vibePreset), cur.vibePreset);
    fill('.ad-tn', tuneSelectHtml(cur.tuneId, cur.soundPreset), cur.soundPreset);
  }
  // The library lists change as the user edits them: rebuild the selects,
  // keeping the current choice.
  function refreshLibraries(): void {
    const v = currentValue();
    cur = { vibeId: v.vibeId, vibePreset: v.vibePattern, tuneId: v.tuneId, soundPreset: v.soundPreset };
    refill();
  }
  function apply(v: any): void {
    v = v || {};
    q('.ad-sm').value = '' + num(v.snoozeMinutes, 0, 120, 10);
    q('.ad-sx').value = '' + num(v.snoozeMax, 0, 20, 3);
    q('.ad-vib').checked = v.vibration !== false;
    q('.ad-snd').checked = v.sound !== false;
    q('.ad-inc').checked = !!v.increasing;
    cur = {
      vibeId: num(v.vibeId, 0, 0x7fffffff, 0), vibePreset: num(v.vibePattern, 0, 255, 0),
      tuneId: num(v.tuneId, 0, 0x7fffffff, 0), soundPreset: num(v.soundPreset, 0, 255, 0),
    };
    refill();
    notes();
  }

  build();
  self._adCurrentValue = currentValue;
  self._adApply = apply;
  apply({});
  if (clayConfig && clayConfig.on && clayConfig.EVENTS) {
    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
      refreshLibraries();
      try { clayConfig.getItemByMessageKey('TuneList').on('change', refreshLibraries); } catch (e) { /* ignore */ }
      try { clayConfig.getItemByMessageKey('VibeList').on('change', refreshLibraries); } catch (e) { /* ignore */ }
    });
  }
  root.addEventListener('input', function() { notes(); self.trigger('change'); });
  root.addEventListener('change', function() { notes(); self.trigger('change'); });
}

const alarmDefaultsComponent = {
  name: 'alarmDefaults',
  template: '<div class="ad-rootc"><div class="ad-body"></div></div>',
  // Same side inset as Clay's own components, so nothing touches the section outline.
  style: '.ad-rootc{padding:0 0.75rem 0.5rem 0.75rem;box-sizing:border-box}' +
    '.ad-sm,.ad-sx,.ad-vp,.ad-tn{box-sizing:border-box;height:2.5rem;margin:0;background-color:#666;color:#fff;border:none;' +
    'border-radius:0.3rem;padding:0 0.6rem;color-scheme:dark;font-size:1rem}' +
    '.ad-vp,.ad-tn{width:100%}.ad-sm,.ad-sx{width:5rem}',
  manipulator: {
    get: function(this: any): any {
      return this._adCurrentValue ? this._adCurrentValue() : {};
    },
    set: function(this: any, value: any) {
      let v: any = value;
      if (v && typeof v === 'object' && v.value !== undefined && v.snoozeMinutes === undefined) { v = v.value; }
      if (typeof v === 'string' && v !== '') { try { v = JSON.parse(v); } catch (e) { v = {}; } }
      if (this._adApply) { this._adApply(v); }
      return this;
    },
  },
  defaults: { label: '' },
  initialize: alarmDefaultsInitialize,
};

export = alarmDefaultsComponent;
