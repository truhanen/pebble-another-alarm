import Clay from 'pebble-clay';
import clayConfig from './config_clay';
import { buildDict, getDefaultClaySettings, ClaySettings } from './dict';

const clay = new Clay(clayConfig, undefined, { autoHandleEvents: false });

// Shared by webviewclosed (raw values straight from Clay's `{ key: { value:
// X } }` shape) and the ready handler below (already-flat values, either
// from localStorage or getDefaultClaySettings) -- unwrapping is defensive,
// since some field types return the plain value directly either way.
function sendConfigToWatch(raw: Record<string, any>) {
  const s: Record<string, any> = {};
  Object.keys(raw).forEach((k) => {
    const v = raw[k];
    s[k] = (v && typeof v === 'object' && 'value' in v) ? v.value : v;
  });
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
  Pebble.sendAppMessage(dict, () => { console.log('config sent'); },
    () => { console.log('config send failed'); });
}

Pebble.addEventListener('showConfiguration', () => {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', (e: any) => {
  if (!e || !e.response) { console.log('No settings changed'); return; }
  // convert=false returns Clay's unflattened `{ key: { value: X } }` shape;
  // sendConfigToWatch unwraps it.
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
