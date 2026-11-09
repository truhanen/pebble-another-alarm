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

// The "New alarm defaults" section is ONE Clay component (alarmDefaults,
// config_alarm_defaults.ts) so it can share the alarm cards' layout; its value
// is this object. The watch still receives the original flat keys
// (DefaultSnoozeMinutes, ...), produced by flattenAlarmDefaults() below.
export interface AlarmDefaults {
  snoozeMinutes: number;   // 0 = snooze off
  snoozeMax: number;       // 0 = unlimited
  vibration: boolean;
  sound: boolean;
  increasing: boolean;     // increasing volume
  vibePattern: number;     // 0 Double, 1 Short, 2 Long
}

export const DEFAULT_ALARM_DEFAULTS: AlarmDefaults = {
  snoozeMinutes: 10, snoozeMax: 3, vibration: true, sound: true, increasing: false, vibePattern: 0,
};

function asBool(v: any, fb: boolean): boolean { return v === undefined || v === null ? fb : !!v; }
function asNum(v: any, fb: number): number {
  const n = parseInt(String(v), 10);
  return Number.isNaN(n) ? fb : n;
}

// Settings saved before the defaults became one component hold them as flat
// keys. Seed AlarmDefaults from those (once) so an update doesn't reset what
// the user had configured. Returns a new object; `saved` is not modified.
export function migrateSavedSettings(saved: Record<string, any>): Record<string, any> {
  const legacy = ['DefaultSnoozeEnabled', 'DefaultSnoozeMinutes', 'DefaultSnoozeMax', 'DefaultSoundEnabled',
    'DefaultVibrationEnabled', 'DefaultIncreasingVolume', 'AlarmVibePattern'];
  const out = { ...saved };
  const unwrapped = (k: string) => { const v = saved[k]; return (v && typeof v === 'object' && 'value' in v) ? v.value : v; };
  if (out.AlarmDefaults !== undefined || !legacy.some((k) => saved[k] !== undefined)) { return out; }
  const enabled = asBool(unwrapped('DefaultSnoozeEnabled'), true);
  out.AlarmDefaults = {
    snoozeMinutes: enabled ? asNum(unwrapped('DefaultSnoozeMinutes'), DEFAULT_ALARM_DEFAULTS.snoozeMinutes) : 0,
    snoozeMax: asNum(unwrapped('DefaultSnoozeMax'), DEFAULT_ALARM_DEFAULTS.snoozeMax),
    vibration: asBool(unwrapped('DefaultVibrationEnabled'), true),
    sound: asBool(unwrapped('DefaultSoundEnabled'), true),
    increasing: asBool(unwrapped('DefaultIncreasingVolume'), false),
    vibePattern: asNum(unwrapped('AlarmVibePattern'), 0),
  };
  return out;
}

// Copies the AlarmDefaults object (if present) onto the flat keys buildDict
// and the watch protocol use. Returns a new object.
export function flattenAlarmDefaults(s: Record<string, any>): Record<string, any> {
  const raw = s.AlarmDefaults;
  const d = (raw && typeof raw === 'object' && 'value' in raw) ? raw.value : raw;
  if (!d || typeof d !== 'object') { return { ...s }; }
  const m = asNum(d.snoozeMinutes, DEFAULT_ALARM_DEFAULTS.snoozeMinutes);
  return {
    ...s,
    DefaultSnoozeEnabled: m > 0,
    DefaultSnoozeMinutes: String(m),
    DefaultSnoozeMax: String(asNum(d.snoozeMax, DEFAULT_ALARM_DEFAULTS.snoozeMax)),
    DefaultSoundEnabled: asBool(d.sound, true),
    DefaultVibrationEnabled: asBool(d.vibration, true),
    DefaultIncreasingVolume: asBool(d.increasing, false),
    AlarmVibePattern: String(asNum(d.vibePattern, 0)),
  };
}

// Clay config item/section shape is intentionally loose here (matches
// config_clay.ts's own untyped array) -- this only cares about messageKey/
// defaultValue, the two fields every component type actually has.
interface ClayConfigItem {
  type: string;
  messageKey?: string;
  defaultValue?: string | number | boolean | unknown[] | object;
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
