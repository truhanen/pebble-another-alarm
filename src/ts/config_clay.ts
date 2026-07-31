const config = [
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'New Alarm Defaults' },
      { type: 'text', defaultValue: 'Defaults for newly created alarms. Can be changed per alarm from the watch\'s alarm edit menu.'},
      { type: 'toggle', messageKey: 'DefaultSnoozeEnabled', label: 'Snooze enabled',
        defaultValue: true },
      { type: 'input', messageKey: 'DefaultSnoozeMinutes', label: 'Snooze duration (minutes)',
        attributes: { type: 'number', min: 1, max: 60 }, defaultValue: '10' },
      // "Repeats", not "Max snoozes", to match the on-watch snooze editor's
      // own field name for the exact same value.
      { type: 'input', messageKey: 'DefaultSnoozeMax', label: 'Snooze repeats (0 = unlimited)',
        attributes: { type: 'number', min: 0, max: 20 }, defaultValue: '3' },
      { type: 'toggle', messageKey: 'DefaultSoundEnabled', label: 'Sound',
        defaultValue: true },
      { type: 'toggle', messageKey: 'DefaultIncreasingVolume', label: 'Increasing volume',
        defaultValue: false },
      { type: 'toggle', messageKey: 'DefaultVibrationEnabled', label: 'Vibration',
        defaultValue: true },
      { type: 'select', messageKey: 'AlarmVibePattern', label: 'Vibration pattern',
        defaultValue: '0', options: [
          { label: 'Double', value: '0' },
          { label: 'Short', value: '1' },
          { label: 'Long', value: '2' },
        ] },
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
      // Unlike everything in "New alarm defaults" below, this isn't a
      // default copied at creation time -- it's read live by every alarm's
      // ring screen (an alarm's own Sound toggle is an additional gate on
      // top of this, not a replacement for it).
      { type: 'slider', messageKey: 'AudioVolume',
        label: 'Alarm volume. Set to 0 to disable sound for all alarms. Used as maximum for alarms with increasing volume.',
        defaultValue: 5, min: 0, max: 100, step: 5 },
    ],
  },
  { type: 'submit', defaultValue: 'Save' },
];

export default config;
