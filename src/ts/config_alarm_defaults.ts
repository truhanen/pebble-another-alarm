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

function alarmDefaultsInitialize(this: any, _minified: any, _clayConfig: any): void {
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

  function build(): void {
    q('.ad-body').innerHTML =
      '<div class="al-chips ad-opts">' + chip('ad-vib', 'Vibration') + chip('ad-snd', 'Sound') + chip('ad-inc', 'Increasing') + '</div>' +
      '<div class="al-grid">' +
        '<span class="al-lbl">Vibration</span><div><select class="ad-vp">' +
          '<option value="0">Double</option><option value="1">Short</option><option value="2">Long</option></select></div>' +
        '<span class="al-lbl">Snooze (min)</span><div><input class="ad-sm" type="number" min="0" max="120"><span class="al-note ad-sm-note"></span></div>' +
        '<span class="al-lbl">Max snoozes</span><div><input class="ad-sx" type="number" min="0" max="20"><span class="al-note ad-sx-note"></span></div>' +
      '</div>';
  }

  function currentValue(): any {
    return {
      snoozeMinutes: num(q('.ad-sm').value, 0, 120, 10),
      snoozeMax: num(q('.ad-sx').value, 0, 20, 3),
      vibration: q('.ad-vib').checked,
      sound: q('.ad-snd').checked,
      increasing: q('.ad-inc').checked,
      vibePattern: num(q('.ad-vp').value, 0, 2, 0),
    };
  }

  function apply(v: any): void {
    v = v || {};
    q('.ad-sm').value = '' + num(v.snoozeMinutes, 0, 120, 10);
    q('.ad-sx').value = '' + num(v.snoozeMax, 0, 20, 3);
    q('.ad-vib').checked = v.vibration !== false;
    q('.ad-snd').checked = v.sound !== false;
    q('.ad-inc').checked = !!v.increasing;
    q('.ad-vp').value = '' + num(v.vibePattern, 0, 2, 0);
    notes();
  }

  build();
  self._adCurrentValue = currentValue;
  self._adApply = apply;
  apply({});
  root.addEventListener('input', function() { notes(); self.trigger('change'); });
  root.addEventListener('change', function() { notes(); self.trigger('change'); });
}

const alarmDefaultsComponent = {
  name: 'alarmDefaults',
  template: '<div class="ad-rootc"><div class="ad-body"></div></div>',
  // Same side inset as Clay's own components, so nothing touches the section outline.
  style: '.ad-rootc{padding:0 0.75rem 0.5rem 0.75rem;box-sizing:border-box}' +
    '.ad-sm,.ad-sx,.ad-vp{box-sizing:border-box;height:2.5rem;margin:0;background-color:#666;color:#fff;border:none;' +
    'border-radius:0.3rem;padding:0 0.6rem;color-scheme:dark;font-size:1rem}' +
    '.ad-vp{width:100%}.ad-sm,.ad-sx{width:5rem}',
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
