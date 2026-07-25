const config = [
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Another alarm' },
      { type: 'radiogroup', messageKey: 'FirstDayOfWeek', label: 'First day of week',
        defaultValue: '1', options: [
          { label: 'Monday', value: '1' },
          { label: 'Sunday', value: '0' },
        ] },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Alarm signal' },
      { type: 'select', messageKey: 'AlarmVibePattern', label: 'Vibration pattern',
        defaultValue: '0', options: [
          { label: 'Double', value: '0' },
          { label: 'Short', value: '1' },
          { label: 'Long', value: '2' },
        ] },
      // Global volume knob, same shape as Instant Timer's audioVolume: 0
      // disables sound entirely, regardless of any alarm's own Sound toggle.
      { type: 'slider', messageKey: 'AudioVolume', label: 'Beep volume (0 to disable)',
        defaultValue: 0, min: 0, max: 100, step: 1 },
      // Pre-fills new alarms' own Vibration/Sound toggles; does not affect
      // existing alarms. A checkboxgroup (not two separate toggles) so both
      // read as one grouped setting under a single label.
      { type: 'checkboxgroup', messageKey: 'DefaultAlarmSignal', label: 'Defaults for new alarms',
        defaultValue: [true, true], options: ['Vibration', 'Sound'] },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Default snooze' },
      { type: 'text', defaultValue: 'Pre-fills new alarms; does not affect existing ones.' },
      { type: 'input', messageKey: 'DefaultSnoozeMinutes', label: 'Duration (minutes)',
        attributes: { type: 'number', min: 1, max: 60 }, defaultValue: '9' },
      { type: 'input', messageKey: 'DefaultSnoozeMax', label: 'Max snoozes (0 = unlimited)',
        attributes: { type: 'number', min: 0, max: 20 }, defaultValue: '3' },
    ],
  },
  { type: 'submit', defaultValue: 'Save' },
];

export default config;
