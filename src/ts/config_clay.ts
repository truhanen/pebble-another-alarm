// Minimal Clay config scaffold. Replace with real alarm settings as they're
// designed; `ExampleSetting` exists only to prove the tsc -> pkjs -> Clay ->
// AppMessage pipeline works end to end.
const config = [
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Another alarm' },
      { type: 'toggle', messageKey: 'ExampleSetting',
        label: 'Example setting', defaultValue: false },
    ],
  },
  { type: 'submit', defaultValue: 'Save' },
];

export default config;
