// The `message_keys` virtual module is generated at build time from
// package.json's `pebble.messageKeys` (build/js/message_keys.json), and
// resolved by the SDK's webpack config — not present as a real file at
// typecheck time. It's a flat { symbolicName: numericId } map; array-syntax
// keys (e.g. "DefaultAlarmSignal[2]") appear under their base name only —
// `messageKeys.DefaultAlarmSignal` is that key's first of N consecutive ids.
declare module 'message_keys' {
  const messageKeys: Record<string, number>;
  export = messageKeys;
}
