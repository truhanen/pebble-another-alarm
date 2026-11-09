// Pure, host-testable serialization + validation for the phone-edited
// Alarms / Tunes lists (see config_alarm_list.ts / config_tune_list.ts).
//
// Wire format (one AppMessage CString per list, like pebble-another-timer's
// TimerConfig): records separated by RS (\x1e), fields by US (\x1f).
//
//   TuneConfig : id US name US pattern
//   AlarmConfig: id US name US enabled US isCron US hour US minute US
//                repeatDays US cron US snoozeMin US snoozeMax US vibration US
//                sound US tuneId US autoDismiss US increasingVolume
//
// Ids >= PHONE_ID_MIN are phone-owned; the watch allocates its own ids far
// below that, so the watch can tell which alarms/tunes a phone push may
// delete (see main.c's apply_alarm_config/apply_tune_config).

export const RS = '\x1e';
export const US = '\x1f';
export const PHONE_ID_MIN = 0x10000;
export const MAX_ALARMS = 16;
export const MAX_TUNES = 20;
export const NAME_MAX = 31;
export const CRON_FIELD_MAX = 27;
export const SOUND_SEGMENTS_MAX = 32;

export interface TuneRec { id: number; name: string; pattern: string; }
export interface AlarmRec {
  id: number;
  name: string;
  enabled: boolean;
  isCron: boolean;
  hour: number;
  minute: number;
  repeatDays: number;      // AC_DAY_* bitmask, bit 0 = Sunday
  cron: string;            // 6 space-separated fields: min hour dom month dow week
  snoozeMinutes: number;   // 0 = snooze off
  snoozeMax: number;       // 0 = unlimited
  vibration: boolean;
  sound: boolean;
  tuneId: number;          // 0 = built-in preset
  autoDismiss: number;     // 0 off, 1 on, 2 on+hide
  increasingVolume: boolean;
}

export function genId(): number {
  return PHONE_ID_MIN + Math.floor(Math.random() * 0x3fff0000);
}

export function sanitizeName(s: string): string {
  // eslint-disable-next-line no-control-regex
  return String(s || '').replace(/[\x00-\x1f]/g, ' ').slice(0, NAME_MAX);
}

// ---- cron ----

const CRON_RANGES: Array<[number, number]> = [[0, 59], [0, 23], [1, 31], [1, 12], [0, 6], [1, 53]];

// Mirrors ac_cron_parse_field (alarm_calc.c): "*", N, N-M, */N, N-M/N2,
// comma lists.
export function validCronField(text: string, lo: number, hi: number): boolean {
  if (!text || text.length > CRON_FIELD_MAX) { return false; }
  const parts = text.split(',');
  for (let i = 0; i < parts.length; i++) {
    const m = /^(\*|(\d+)(?:-(\d+))?)(?:\/(\d+))?$/.exec(parts[i]);
    if (!m) { return false; }
    if (m[4] !== undefined && parseInt(m[4], 10) < 1) { return false; }
    if (m[1] === '*') { continue; }
    const a = parseInt(m[2], 10);
    const b = m[3] !== undefined ? parseInt(m[3], 10) : a;
    if (a < lo || b > hi || a > b) { return false; }
  }
  return true;
}

// Pads a short pattern with "*" (e.g. "*/20 9-17" -> 6 fields). Returns null
// if there are more than 6 fields or any field is invalid.
export function normalizeCron(text: string): string | null {
  const f = String(text || '').trim().split(/\s+/);
  if (f.length === 0 || f[0] === '' || f.length > 6) { return null; }
  while (f.length < 6) { f.push('*'); }
  for (let i = 0; i < 6; i++) {
    if (!validCronField(f[i], CRON_RANGES[i][0], CRON_RANGES[i][1])) { return null; }
  }
  return f.join(' ');
}

// ---- ABC tune patterns ----

// Mirrors ac_sound_pattern_parse's grammar (see CLAUDE.md "Custom
// sound/vibe patterns"): tokens are `z[N][/D]` rests or
// `[^|_]LETTER[,,,|']  [N][/D]` notes, N/D in 1-8.
export function validTunePattern(text: string): boolean {
  const toks = String(text || '').trim().split(/\s+/);
  if (toks.length === 0 || toks[0] === '' || toks.length > SOUND_SEGMENTS_MAX) { return false; }
  const ratio = '(?:[1-8](?:\\/[1-8])?|\\/[1-8])?';
  const note = new RegExp('^[\\^_]?[C-GAB](?:\'|,{1,8})?' + ratio + '$');
  const rest = new RegExp('^z' + ratio + '$');
  for (let i = 0; i < toks.length; i++) {
    if (!note.test(toks[i]) && !rest.test(toks[i])) { return false; }
  }
  return true;
}

// ---- serialization ----

function b(v: boolean): string { return v ? '1' : '0'; }
function clampInt(v: any, lo: number, hi: number, fb: number): number {
  const n = parseInt(String(v), 10);
  if (isNaN(n)) { return fb; }
  return n < lo ? lo : n > hi ? hi : n;
}

export function tunesToString(list: TuneRec[]): string {
  const out: string[] = [];
  for (let i = 0; i < list.length && out.length < MAX_TUNES; i++) {
    const t = list[i];
    const name = sanitizeName(t.name).trim();
    const pat = String(t.pattern || '').trim();
    if (!name || !validTunePattern(pat)) { continue; }
    out.push([t.id || genId(), name, pat].join(US));
  }
  return out.join(RS);
}

export function alarmsToString(list: AlarmRec[], validTuneIds: number[]): string {
  const out: string[] = [];
  for (let i = 0; i < list.length && out.length < MAX_ALARMS; i++) {
    const a = list[i];
    let cron = '*';
    if (a.isCron) {
      const n = normalizeCron(a.cron);
      if (n === null) { continue; }
      cron = n;
    }
    const tuneId = validTuneIds.indexOf(a.tuneId) >= 0 ? a.tuneId : 0;
    out.push([
      a.id || genId(),
      sanitizeName(a.name),
      b(a.enabled),
      b(a.isCron),
      clampInt(a.hour, 0, 23, 7),
      clampInt(a.minute, 0, 59, 0),
      clampInt(a.repeatDays, 0, 127, 0),
      cron,
      clampInt(a.snoozeMinutes, 0, 120, 10),
      clampInt(a.snoozeMax, 0, 20, 3),
      b(a.vibration),
      b(a.sound),
      tuneId,
      clampInt(a.autoDismiss, 0, 2, 0),
      b(a.increasingVolume),
    ].join(US));
  }
  return out.join(RS);
}
