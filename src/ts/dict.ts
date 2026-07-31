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
