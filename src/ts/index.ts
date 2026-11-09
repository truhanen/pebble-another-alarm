import Clay from 'pebble-clay';
import clayConfig from './config_clay';
import { buildDict, getDefaultClaySettings, ClaySettings } from './dict';
import clayConfigCustom from './config_clay_custom';
import alarmListComponent from './config_alarm_list';
import tuneListComponent from './config_tune_list';
import { tunesToString, alarmsToString, TuneRec, AlarmRec } from './list_config';

const clay = new Clay(clayConfig, clayConfigCustom, { autoHandleEvents: false });
clay.registerComponent(alarmListComponent);
clay.registerComponent(tuneListComponent);

// The Alarms/Tunes lists are phone-authoritative but the watch edits alarms
// too (e.g. short-press enable/disable), so they are only pushed when the
// user actually saved the config page -- NOT on every launch like the plain
// settings, which would silently revert watch-side edits. If that push
// can't be delivered (watch app not running), this flag keeps it pending
// until the next `ready`.
const LISTS_PENDING_KEY = 'lists_pending';

function unwrap(v: any): any {
  return (v && typeof v === 'object' && !Array.isArray(v) && 'value' in v) ? v.value : v;
}

// Sends TuneConfig then AlarmConfig (tunes first so an alarm's tune id
// resolves on the watch). One AppMessage in flight at a time.
function sendLists(s: Record<string, any>) {
  const tunes: TuneRec[] = Array.isArray(s.TuneList) ? s.TuneList : [];
  const alarms: AlarmRec[] = Array.isArray(s.AlarmList) ? s.AlarmList : [];
  const tuneStr = tunesToString(tunes);
  const validIds = tuneStr === '' ? [] : tuneStr.split('\x1e').map((r) => parseInt(r.split('\x1f')[0], 10));
  const alarmStr = alarmsToString(alarms, validIds);
  Pebble.sendAppMessage({ TuneConfig: tuneStr }, () => {
    Pebble.sendAppMessage({ AlarmConfig: alarmStr }, () => {
      console.log('lists sent');
      try { localStorage.removeItem(LISTS_PENDING_KEY); } catch (e) { /* ignore */ }
    }, () => { console.log('alarm list send failed'); });
  }, () => { console.log('tune list send failed'); });
}

// Shared by webviewclosed (raw values straight from Clay's `{ key: { value:
// X } }` shape) and the ready handler below (already-flat values, either
// from localStorage or getDefaultClaySettings) -- unwrapping is defensive,
// since some field types return the plain value directly either way.
function sendConfigToWatch(raw: Record<string, any>) {
  const s: Record<string, any> = {};
  Object.keys(raw).forEach((k) => { s[k] = unwrap(raw[k]); });
  const settings: ClaySettings = {
    FirstDayOfWeek: s.FirstDayOfWeek,
    DateFormat: s.DateFormat,
    AlarmVibePattern: s.AlarmVibePattern,
    DefaultSnoozeMinutes: s.DefaultSnoozeMinutes,
    DefaultSnoozeMax: s.DefaultSnoozeMax,
    DefaultSnoozeEnabled: s.DefaultSnoozeEnabled,
    DefaultSoundEnabled: s.DefaultSoundEnabled,
    DefaultVibrationEnabled: s.DefaultVibrationEnabled,
    DefaultIncreasingVolume: s.DefaultIncreasingVolume,
    AudioVolume: s.AudioVolume,
  };
  const dict = buildDict(settings);
  const afterSettings = () => {
    let pending = false;
    try { pending = localStorage.getItem(LISTS_PENDING_KEY) === '1'; } catch (e) { /* ignore */ }
    if (pending) { sendLists(s); }
  };
  Pebble.sendAppMessage(dict, () => { console.log('config sent'); afterSettings(); },
    () => { console.log('config send failed'); });
}

Pebble.addEventListener('showConfiguration', () => {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', (e: any) => {
  if (!e || !e.response) { console.log('No settings changed'); return; }
  // convert=false returns Clay's unflattened `{ key: { value: X } }` shape;
  // sendConfigToWatch unwraps it. A Save always (re)pushes the lists.
  try { localStorage.setItem(LISTS_PENDING_KEY, '1'); } catch (err) { /* ignore */ }
  sendConfigToWatch(clay.getSettings(e.response, false));
});

// Runs on every phone-side launch, not just after the user opens the config
// page and hits Save -- otherwise the watch is stuck on its own hardcoded
// fallback defaults (see alarm_store.c's store_load_* functions) until that
// first Save ever happens, which most users never think to do. Clay itself
// persists saved settings to localStorage['clay-settings'] on every Save
// (pebble-clay/index.js's getSettings()); merging that over
// getDefaultClaySettings means a fresh install sends real defaults
// immediately, and a config field added in a later app update still reaches
// existing users who saved before it existed, without needing them to
// reopen the config page again.
Pebble.addEventListener('ready', () => {
  let saved: Record<string, any> = {};
  try {
    saved = JSON.parse(localStorage.getItem('clay-settings') || '{}');
  } catch (e) {
    console.log('clay-settings in localStorage was not valid JSON, using defaults only');
  }
  sendConfigToWatch({ ...getDefaultClaySettings(clayConfig), ...saved });
});
