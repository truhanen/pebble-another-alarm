import Clay from 'pebble-clay';
import clayConfig from './config_clay';
import { buildDict, ClaySettings } from './dict';

const clay = new Clay(clayConfig, undefined, { autoHandleEvents: false });

Pebble.addEventListener('showConfiguration', () => {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', (e: any) => {
  if (!e || !e.response) { console.log('No settings changed'); return; }
  // convert=false returns Clay's unflattened `{ key: { value: X } }` shape;
  // unwrap it defensively (some field types return the plain value directly).
  const raw = clay.getSettings(e.response, false);
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
});
