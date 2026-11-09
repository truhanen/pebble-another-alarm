import { BUILTIN_TUNES, BUILTIN_VIBES, TUNE_HELP, VIBE_HELP, CRON_HELP } from './pattern_help';

const config = [
  {
    type: 'section',
    items: [
      // Each top-level section starts with the one `heading` the accordion
      // (config_clay_custom.ts) keys on -- anything else inside a section
      // must be a non-heading item.
      { type: 'heading', defaultValue: 'New alarm defaults' },
      { type: 'text', defaultValue: 'Defaults for newly created alarms. Can be changed per alarm from the watch\'s alarm edit menu.'},
      // One component (config_alarm_defaults.ts) laid out like an alarm card's
      // signal/snooze block; index.ts/dict.ts flatten it into the watch's
      // original DefaultSnooze*/DefaultSound*/... keys. "Repeats"/"Max
      // snoozes" is the same value the on-watch snooze editor calls Repeats.
      { type: 'alarmDefaults', messageKey: 'AlarmDefaults',
        defaultValue: { snoozeMinutes: 10, snoozeMax: 3, vibration: true, sound: true,
                        increasing: false, vibePattern: 0 } },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Alarms' },
      // Phone-edited list, pushed to the watch on Save (see list_config.ts).
      // Clay-only key: index.ts serializes it into the real `AlarmConfig` key.
      // `help` is read by the component (this.config): the watch's full cron
      // help, shown collapsible under each cron alarm's Basic/Cron selector.
      { type: 'alarmList', messageKey: 'AlarmList', defaultValue: [], help: CRON_HELP },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Tunes' },
      // `builtins`/`help` are read by the component (this.config): the watch's
      // built-in tunes, shown read-only, and the long pattern-syntax help.
      { type: 'tuneList', messageKey: 'TuneList', defaultValue: [],
        builtins: BUILTIN_TUNES, help: TUNE_HELP },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Vibration patterns' },
      // Named custom vibration patterns (the library alarms pick from);
      // Clay-only key serialized into the real `VibeConfig` key by index.ts.
      { type: 'vibeList', messageKey: 'VibeList', defaultValue: [],
        builtins: BUILTIN_VIBES, help: VIBE_HELP },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Other' },
      { type: 'radiogroup', messageKey: 'FirstDayOfWeek', label: 'First day of week',
        defaultValue: '1', options: [
          { label: 'Monday', value: '1' },
          { label: 'Sunday', value: '0' },
        ] },
      // Used in the "next occurrence" text (main list bottom bar, cron
      // editor's Apply-row preview) whenever it falls back to showing a
      // bare date -- Pebble's C API exposes a 12h/24h time-format query
      // (clock_is_24h_style()) but nothing equivalent for date order, so
      // unlike time this needs its own manual setting.
      { type: 'select', messageKey: 'DateFormat', label: 'Date format',
        defaultValue: '0', options: [
          { label: 'day.month.', value: '0' },
          { label: 'month/day', value: '1' },
        ] },
      // Unlike everything in "New alarm defaults", this isn't a
      // default copied at creation time -- it's read live by every alarm's
      // ring screen (an alarm's own Sound toggle is an additional gate on
      // top of this, not a replacement for it).
      { type: 'slider', messageKey: 'AudioVolume',
        label: 'Alarm volume. Set to 0 to disable sound for all alarms. Used as maximum for alarms with increasing volume.',
        defaultValue: 50, min: 0, max: 100, step: 5 },
    ],
  },
  { type: 'submit', defaultValue: 'Save' },
];

export default config;
