// Pure, host-testable transform from raw Clay settings to an AppMessage dict.
// Kept separate from index.ts (which touches the Pebble/Clay APIs) so it can
// be unit-tested without mocking the Pebble runtime.
//
// index.ts builds this dict manually (autoHandleEvents: false), so unlike a
// project relying on Clay's own auto-submit path, radiogroup/select values
// are parsed to real int32s here — the watch never needs to unwrap an
// ASCII-digit char (the workaround some Clay-based apps need instead).
export interface ClaySettings {
  FirstDayOfWeek?: string;
  DateFormat?: string;
  AlarmVibePattern?: string;
  DefaultSnoozeMinutes?: string;
  DefaultSnoozeMax?: string;
  DefaultSnoozeEnabled?: boolean;    // Clay's toggle type returns a boolean
  DefaultSoundEnabled?: boolean;
  DefaultVibrationEnabled?: boolean;
  DefaultIncreasingVolume?: boolean;
  AudioVolume?: string | number;     // Clay's slider type returns a number, not a string
}

// Clay config item/section shape is intentionally loose here (matches
// config_clay.ts's own untyped array) -- this only cares about messageKey/
// defaultValue, the two fields every component type actually has.
interface ClayConfigItem {
  type: string;
  messageKey?: string;
  defaultValue?: string | number | boolean;
  items?: ClayConfigItem[];
}

// Walks a Clay config array (mirrors Clay's own internal _scanConfig
// recursion in pebble-clay/index.js) and collects each item's own
// defaultValue by messageKey. This is what a first-ever launch (before the
// user has ever opened the phone config page, so there's nothing in
// localStorage yet) sends to the watch -- keeping config_clay.ts as the
// single place defaults are declared, rather than a second hardcoded object
// that could drift out of sync with it.
export function getDefaultClaySettings(config: ClayConfigItem[]): Record<string, any> {
  const out: Record<string, any> = {};
  function scan(item: ClayConfigItem) {
    if (item.items) { item.items.forEach(scan); return; }
    if (item.messageKey !== undefined) { out[item.messageKey] = item.defaultValue; }
  }
  config.forEach(scan);
  return out;
}

function toInt(value: string | number | undefined, fallback: number): number {
  const n = parseInt(String(value ?? ''), 10);
  return Number.isNaN(n) ? fallback : n;
}

export function buildDict(settings: ClaySettings): Record<string, number> {
  const snoozeEnabled = settings.DefaultSnoozeEnabled ?? true;
  return {
    FirstDayOfWeek: toInt(settings.FirstDayOfWeek, 1),
    DateFormat: toInt(settings.DateFormat, 0),
    AlarmVibePattern: toInt(settings.AlarmVibePattern, 0),
    // 0 reuses the watch's existing "snooze disabled outright" convention
    // (snooze_minutes == 0) rather than sending a wholly separate flag for
    // the watch to also track — when snooze is off, the actual duration
    // value is moot, so there's nothing lost by collapsing it to 0 here.
    DefaultSnoozeMinutes: snoozeEnabled ? toInt(settings.DefaultSnoozeMinutes, 9) : 0,
    DefaultSnoozeMax: toInt(settings.DefaultSnoozeMax, 3),
    DefaultSoundEnabled: (settings.DefaultSoundEnabled ?? true) ? 1 : 0,
    DefaultVibrationEnabled: (settings.DefaultVibrationEnabled ?? true) ? 1 : 0,
    DefaultIncreasingVolume: (settings.DefaultIncreasingVolume ?? false) ? 1 : 0,
    AudioVolume: toInt(settings.AudioVolume, 0),
  };
}
