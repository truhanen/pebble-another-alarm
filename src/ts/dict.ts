// Pure, host-testable transform from raw Clay settings to an AppMessage dict.
// Kept separate from index.ts (which touches the Pebble/Clay APIs) so it can
// be unit-tested without mocking the Pebble runtime.
export interface ClaySettings {
  ExampleSetting?: boolean;
}

export function buildDict(settings: ClaySettings): Record<string, number> {
  return {
    ExampleSetting: settings.ExampleSetting ? 1 : 0,
  };
}
