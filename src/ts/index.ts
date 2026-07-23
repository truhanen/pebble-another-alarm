import Clay from 'pebble-clay';
import clayConfig from './config_clay';
import { buildDict, ClaySettings } from './dict';

const clay = new Clay(clayConfig, undefined, { autoHandleEvents: false });

Pebble.addEventListener('showConfiguration', () => {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', (e: any) => {
  if (!e || !e.response) { console.log('No settings changed'); return; }
  const raw = clay.getSettings(e.response, false);
  const settings: ClaySettings = {
    ExampleSetting: !!raw.ExampleSetting,
  };
  const dict = buildDict(settings);
  Pebble.sendAppMessage(dict, () => { console.log('config sent'); },
    () => { console.log('config send failed'); });
});
