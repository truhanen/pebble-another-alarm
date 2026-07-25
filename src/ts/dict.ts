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
  AlarmVibePattern?: string;
  DefaultSnoozeMinutes?: string;
  DefaultSnoozeMax?: string;
  AudioVolume?: string | number;   // Clay's slider type returns a number, not a string
  DefaultAlarmSignal?: boolean[];  // Clay's checkboxgroup type returns an array of booleans
}

// The base numeric id of the DefaultAlarmSignal[2] array-type message key
// (see package.json). Passed in rather than required directly here so this
// module stays a pure function of its inputs, host-testable without needing
// the generated `message_keys` module (which only exists at build time).
export interface MessageKeyIds {
  DefaultAlarmSignal: number;
}

function toInt(value: string | number | undefined, fallback: number): number {
  const n = parseInt(String(value ?? ''), 10);
  return Number.isNaN(n) ? fallback : n;
}

export function buildDict(settings: ClaySettings, keys: MessageKeyIds): Record<string, number> {
  const signal = settings.DefaultAlarmSignal ?? [];
  const vibration = (signal[0] ?? true) ? 1 : 0;
  const sound = (signal[1] ?? true) ? 1 : 0;
  return {
    FirstDayOfWeek: toInt(settings.FirstDayOfWeek, 1),
    AlarmVibePattern: toInt(settings.AlarmVibePattern, 0),
    DefaultSnoozeMinutes: toInt(settings.DefaultSnoozeMinutes, 9),
    DefaultSnoozeMax: toInt(settings.DefaultSnoozeMax, 3),
    AudioVolume: toInt(settings.AudioVolume, 0),
    // Sent as two plain scalar ints at their own explicit numeric keys
    // (DefaultAlarmSignal[2]'s base id = Vibration, base+1 = Sound), NOT as
    // one key holding a JS array value. A JS array VALUE passed to
    // Pebble.sendAppMessage is not guaranteed to be split across an
    // array-type key's reserved ids by every PebbleKitJS runtime (confirmed
    // for pypkjs it instead packs into a single byte-array tuple at the base
    // id, which the previous version of this code got wrong on the read
    // side) — addressing each element's own numeric key directly sidesteps
    // that ambiguity entirely, since these are then just two ordinary
    // scalar sends like every other key here.
    [keys.DefaultAlarmSignal]: vibration,
    [keys.DefaultAlarmSignal + 1]: sound,
  };
}
