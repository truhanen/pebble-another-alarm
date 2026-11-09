import Clay from 'pebble-clay';
import clayConfig from './config_clay';
import { buildDict, getDefaultClaySettings, ClaySettings } from './dict';
import clayConfigCustom from './config_clay_custom';
import alarmListComponent from './config_alarm_list';
import tuneListComponent from './config_tune_list';
import { tunesToString, alarmsToString, parseTunes, parseAlarms, TuneRec, AlarmRec } from './list_config';

const clay = new Clay(clayConfig, clayConfigCustom, { autoHandleEvents: false });
clay.registerComponent(alarmListComponent);
clay.registerComponent(tuneListComponent);

// The watch sends a full snapshot of its alarms and tunes (same record format
// as a push, see list_config.ts) at launch and after every change, and the
// page lists that, so existing watch alarms/tunes show up and stay current.
// The page's own edits are pushed back only when the user saves -- NOT on
// every launch like the plain settings, which would silently revert
// watch-side edits. If a push can't be delivered (watch app not running),
// LISTS_PENDING_KEY keeps it pending until the next `ready`, and snapshots
// from the watch are ignored meanwhile (they'd overwrite the unsent edits).
const LISTS_PENDING_KEY = 'lists_pending';
// Ids that were in the lists when the config page was opened. Items present
// then but gone at Save were deleted on the page; only those get tombstones
// (never "everything the page doesn't list", which would delete watch items
// created after the page opened).
const OPEN_ALARM_IDS_KEY = 'cfg_open_alarm_ids';
const OPEN_TUNE_IDS_KEY = 'cfg_open_tune_ids';
// Deletions not yet delivered to the watch.
const ALARM_TOMB_KEY = 'alarm_tombstones';
const TUNE_TOMB_KEY = 'tune_tombstones';

function unwrap(v: any): any {
  return (v && typeof v === 'object' && !Array.isArray(v) && 'value' in v) ? v.value : v;
}

function readJson(key: string, fallback: any): any {
  try {
    const raw = localStorage.getItem(key);
    return raw === null ? fallback : JSON.parse(raw);
  } catch (e) { return fallback; }
}
function writeJson(key: string, v: any) {
  try { localStorage.setItem(key, JSON.stringify(v)); } catch (e) { /* ignore */ }
}
function idsOf(list: any): number[] {
  return Array.isArray(list) ? list.map((x: any) => x && x.id).filter((n: any) => typeof n === 'number') : [];
}

// Sends TuneConfig then AlarmConfig (tunes first so an alarm's tune id
// resolves on the watch). One AppMessage in flight at a time.
function sendLists(s: Record<string, any>) {
  const tunes: TuneRec[] = Array.isArray(s.TuneList) ? s.TuneList : [];
  const alarms: AlarmRec[] = Array.isArray(s.AlarmList) ? s.AlarmList : [];
  const tuneTombs: number[] = readJson(TUNE_TOMB_KEY, []);
  const alarmTombs: number[] = readJson(ALARM_TOMB_KEY, []);
  const tuneStr = tunesToString(tunes, tuneTombs);
  const validIds = tunesToString(tunes).split('\x1e').map((r) => parseInt(r.split('\x1f')[0], 10));
  const alarmStr = alarmsToString(alarms, validIds, alarmTombs);
  Pebble.sendAppMessage({ TuneConfig: tuneStr }, () => {
    Pebble.sendAppMessage({ AlarmConfig: alarmStr }, () => {
      console.log('lists sent');
      try {
        localStorage.removeItem(LISTS_PENDING_KEY);
        localStorage.removeItem(ALARM_TOMB_KEY);
        localStorage.removeItem(TUNE_TOMB_KEY);
      } catch (e) { /* ignore */ }
    }, () => { console.log('alarm list send failed'); });
  }, () => { console.log('tune list send failed'); });
}

// Snapshot from the watch: replace the phone's copy of the lists (which is
// what the config page is built from), unless unsent page edits are pending.
Pebble.addEventListener('appmessage', (e: any) => {
  const p = e && e.payload;
  if (!p || (typeof p.TuneConfig !== 'string' && typeof p.AlarmConfig !== 'string')) { return; }
  let pending = false;
  try { pending = localStorage.getItem(LISTS_PENDING_KEY) === '1'; } catch (err) { /* ignore */ }
  if (pending) { console.log('ignoring watch snapshot: unsent page edits pending'); return; }
  const cs: Record<string, any> = readJson('clay-settings', {}) || {};
  if (typeof p.TuneConfig === 'string') { cs.TuneList = parseTunes(p.TuneConfig); }
  if (typeof p.AlarmConfig === 'string') { cs.AlarmList = parseAlarms(p.AlarmConfig); }
  writeJson('clay-settings', cs);
  console.log('watch snapshot stored: ' + (Array.isArray(cs.AlarmList) ? cs.AlarmList.length : '-') +
    ' alarms, ' + (Array.isArray(cs.TuneList) ? cs.TuneList.length : '-') + ' tunes');
});

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
  const cs: Record<string, any> = readJson('clay-settings', {}) || {};
  writeJson(OPEN_ALARM_IDS_KEY, idsOf(cs.AlarmList));
  writeJson(OPEN_TUNE_IDS_KEY, idsOf(cs.TuneList));
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', (e: any) => {
  if (!e || !e.response) { console.log('No settings changed'); return; }
  // convert=false returns Clay's unflattened `{ key: { value: X } }` shape;
  // sendConfigToWatch unwraps it. A Save always (re)pushes the lists.
  const saved = clay.getSettings(e.response, false);
  // Items listed when the page opened but gone now were deleted on the page:
  // remember them (persisted until delivered) so the watch removes them too.
  const addTombs = (key: string, openKey: string, now: any) => {
    const present = idsOf(unwrap(now));
    const tombs: number[] = readJson(key, []);
    readJson(openKey, []).forEach((id: number) => {
      if (present.indexOf(id) < 0 && tombs.indexOf(id) < 0) { tombs.push(id); }
    });
    writeJson(key, tombs.filter((id) => present.indexOf(id) < 0));
  };
  addTombs(ALARM_TOMB_KEY, OPEN_ALARM_IDS_KEY, saved.AlarmList);
  addTombs(TUNE_TOMB_KEY, OPEN_TUNE_IDS_KEY, saved.TuneList);
  try { localStorage.setItem(LISTS_PENDING_KEY, '1'); } catch (err) { /* ignore */ }
  sendConfigToWatch(saved);
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
