// Pure, host-testable serialization + validation for the phone-edited
// Alarms / Tunes lists (see config_alarm_list.ts / config_tune_list.ts).
//
// Wire format (one AppMessage CString per list, like pebble-another-timer's
// TimerConfig): records separated by RS (\x1e), fields by US (\x1f).
//
//   TuneConfig : id US name US pattern
//   VibeConfig : id US name US pattern   (vibration library, same shape)
//   AlarmConfig: id US name US enabled US isCron US hour US minute US
//                repeatDays US cron US snoozeMin US snoozeMax US vibration US
//                sound US tuneId US autoDismiss US increasingVolume US
//                vibeId US vibePreset      (the last two are optional on read)
//
// The watch sends the SAME formats back (a full snapshot of everything it
// has, after every change and at launch), so the page lists existing watch
// alarms/tunes too. A record with a single field (just the id) in a push
// means "delete this id" -- a tombstone for an item removed on the page.
// Ids >= PHONE_ID_MIN are minted by the phone; the watch allocates its own
// far below that, so the two can never collide (see main.c's
// apply_alarm_config/apply_tune_config).

export const RS = '\x1e';
export const US = '\x1f';
export const PHONE_ID_MIN = 0x10000;
export const MAX_ALARMS = 16;
export const MAX_TUNES = 20;
export const MAX_VIBES = 20;
export const NAME_MAX = 31;
export const CRON_FIELD_MAX = 27;
export const SOUND_SEGMENTS_MAX = 32;
// The watch's keyboard caps a tune pattern at this many characters, and it keeps
// every snapshot well inside the 8200-byte AppMessage buffers; the watch rejects
// longer ones from a push, so the page must too.
export const PATTERN_MAX_CHARS = 160;
export const VIBE_SEGMENTS_MAX = 8;

export interface TuneRec { id: number; name: string; pattern: string; }
export interface VibeRec { id: number; name: string; pattern: string; }
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
  vibeId: number;          // library vibration, 0 = a preset
  vibePreset: number;      // 0 Double, 1 Short, 2 Long (used when vibeId is 0)
}

export function genId(): number {
  return PHONE_ID_MIN + Math.floor(Math.random() * 0x3fff0000);
}

// Names are stored on the watch in a 31-BYTE buffer, so cut at 31 UTF-8 bytes
// (on a character boundary), not 31 UTF-16 units: a few emoji or accented
// letters would otherwise be sliced mid-character on the watch.
export function sanitizeName(s: string): string {
  // eslint-disable-next-line no-control-regex
  const clean = String(s || '').replace(/[\x00-\x1f]/g, ' ');
  let out = '';
  let bytes = 0;
  for (let i = 0; i < clean.length; i++) {
    let ch = clean.charAt(i);
    const c = clean.charCodeAt(i);
    if (c >= 0xd800 && c <= 0xdbff && i + 1 < clean.length) { ch += clean.charAt(++i); }   // surrogate pair
    const cp = ch.length === 2 ? 0x10000 : c;
    const n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    if (bytes + n > NAME_MAX) { break; }
    out += ch;
    bytes += n;
  }
  return out;
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

// Expands one field into the set of matching values, or null if invalid.
function fieldSet(text: string, lo: number, hi: number): { [v: number]: boolean } | null {
  if (!validCronField(text, lo, hi)) { return null; }
  const set: { [v: number]: boolean } = {};
  text.split(',').forEach((part) => {
    const m = /^(\*|(\d+)(?:-(\d+))?)(?:\/(\d+))?$/.exec(part) as RegExpExecArray;
    const step = m[4] !== undefined ? parseInt(m[4], 10) : 1;
    let a = lo, b = hi;
    if (m[1] !== '*') { a = parseInt(m[2], 10); b = m[3] !== undefined ? parseInt(m[3], 10) : a; }
    for (let v = a; v <= b; v += step) { set[v] = true; }
  });
  return set;
}

function isoWeek(d: Date): number {
  const t = new Date(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()));
  const dn = t.getUTCDay() || 7;
  t.setUTCDate(t.getUTCDate() + 4 - dn);
  const y0 = new Date(Date.UTC(t.getUTCFullYear(), 0, 1));
  return Math.ceil(((t.getTime() - y0.getTime()) / 86400000 + 1) / 7);
}

// First minute strictly after `now` matching the cron pattern (same 400-day
// horizon as the watch's ac_cron_next_offset_days), or null. Mirrors the
// live "Next: ..." preview in config_alarm_list.ts.
export function cronNext(text: string, now: Date): Date | null {
  const n = normalizeCron(text);
  if (n === null) { return null; }
  const f = n.split(' ');
  const sets = f.map((x, i) => fieldSet(x, CRON_RANGES[i][0], CRON_RANGES[i][1]) as { [v: number]: boolean });
  for (let d = 0; d <= 400; d++) {
    const day = new Date(now.getFullYear(), now.getMonth(), now.getDate() + d);
    if (!sets[3][day.getMonth() + 1] || !sets[2][day.getDate()] || !sets[4][day.getDay()] || !sets[5][isoWeek(day)]) { continue; }
    for (let h = 0; h < 24; h++) {
      if (!sets[1][h]) { continue; }
      for (let m = 0; m < 60; m++) {
        if (!sets[0][m]) { continue; }
        const c = new Date(day.getFullYear(), day.getMonth(), day.getDate(), h, m);
        if (c.getTime() > now.getTime()) { return c; }
      }
    }
  }
  return null;
}

// ---- ABC tune patterns ----

// Mirrors ac_sound_pattern_parse's grammar (see CLAUDE.md "Custom
// sound/vibe patterns"): tokens are `z[N][/D]` rests or
// `[^|_]LETTER[,,,|']  [N][/D]` notes, N/D in 1-8.
export function validTunePattern(text: string): boolean {
  // The watch's parser splits on plain spaces only (a tab is not a separator).
  const trimmed = String(text || '').replace(/^ +| +$/g, '');
  if (trimmed.length > PATTERN_MAX_CHARS) { return false; }
  const toks = trimmed.split(/ +/);
  if (toks.length === 0 || toks[0] === '' || toks.length > SOUND_SEGMENTS_MAX) { return false; }
  const ratio = '(?:[1-8](?:\\/[1-8])?|\\/[1-8])?';
  const note = new RegExp('^[\\^_]?[C-GAB](?:\'|,{1,8})?' + ratio + '$');
  const rest = new RegExp('^z' + ratio + '$');
  for (let i = 0; i < toks.length; i++) {
    if (!note.test(toks[i]) && !rest.test(toks[i])) { return false; }
  }
  return true;
}

// Vibration patterns (ac_vibe_pattern_parse): the same ratio/rest tokens but no
// pitch, so an on-beat is just its ratio (`2`, `1/2`, `/2`) and `z[ratio]` is
// a rest; at most 8 segments.
export function validVibePattern(text: string): boolean {
  const toks = String(text || '').replace(/^ +| +$/g, '').split(/ +/);
  if (toks.length === 0 || toks[0] === '' || toks.length > VIBE_SEGMENTS_MAX) { return false; }
  const on = /^(?:[1-8](?:\/[1-8])?|\/[1-8])$/;
  const rest = /^z(?:[1-8](?:\/[1-8])?|\/[1-8])?$/;
  for (let i = 0; i < toks.length; i++) {
    if (!on.test(toks[i]) && !rest.test(toks[i])) { return false; }
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

// Tombstone records: just the id. Skipped for ids still in `list`.
function tombstones(removed: number[], present: number[]): string[] {
  const out: string[] = [];
  for (let i = 0; i < removed.length; i++) {
    if (removed[i] > 0 && present.indexOf(removed[i]) < 0 && out.indexOf(String(removed[i])) < 0) { out.push(String(removed[i])); }
  }
  return out;
}

export function tunesToString(list: TuneRec[], removed: number[] = []): string {
  const out: string[] = [];
  for (let i = 0; i < list.length && out.length < MAX_TUNES; i++) {
    const t = list[i];
    const name = sanitizeName(t.name).trim();
    const pat = String(t.pattern || '').trim();
    if (!name || !validTunePattern(pat)) { continue; }
    out.push([t.id || genId(), name, pat].join(US));
  }
  const present = list.map((t) => t.id);
  return out.concat(tombstones(removed, present)).join(RS);
}

export function vibesToString(list: VibeRec[], removed: number[] = []): string {
  const out: string[] = [];
  for (let i = 0; i < list.length && out.length < MAX_VIBES; i++) {
    const v = list[i];
    const name = sanitizeName(v.name).trim();
    const pat = String(v.pattern || '').trim();
    if (!name || !validVibePattern(pat)) { continue; }
    out.push([v.id || genId(), name, pat].join(US));
  }
  const present = list.map((v) => v.id);
  return out.concat(tombstones(removed, present)).join(RS);
}

export function alarmsToString(list: AlarmRec[], validTuneIds: number[], removed: number[] = [], validVibeIds: number[] = []): string {
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
    const vibeId = validVibeIds.indexOf(a.vibeId) >= 0 ? a.vibeId : 0;
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
      vibeId,
      clampInt(a.vibePreset, 0, 2, 0),
    ].join(US));
  }
  const present = list.map((a) => a.id);
  return out.concat(tombstones(removed, present)).join(RS);
}

// ---- parsing (watch snapshot -> page lists) ----

function toInt(v: string, fb: number): number {
  const n = parseInt(v, 10);
  return isNaN(n) ? fb : n;
}

export function parseTunes(str: string): TuneRec[] {
  const out: TuneRec[] = [];
  if (!str) { return out; }
  str.split(RS).forEach((rec) => {
    const f = rec.split(US);
    if (f.length < 3) { return; }
    const id = toInt(f[0], 0);
    if (id > 0 && f[1]) { out.push({ id: id, name: f[1], pattern: f[2] }); }
  });
  return out;
}

export function parseVibes(str: string): VibeRec[] {
  return parseTunes(str);   // same record shape
}

export function parseAlarms(str: string): AlarmRec[] {
  const out: AlarmRec[] = [];
  if (!str) { return out; }
  str.split(RS).forEach((rec) => {
    const f = rec.split(US);
    if (f.length < 15) { return; }
    const id = toInt(f[0], 0);
    if (id <= 0) { return; }
    const isCron = f[3] === '1';
    out.push({
      id: id,
      name: f[1],
      enabled: f[2] === '1',
      isCron: isCron,
      hour: toInt(f[4], 7),
      minute: toInt(f[5], 0),
      repeatDays: toInt(f[6], 0),
      cron: isCron ? f[7] : '0 7 * * *',
      snoozeMinutes: toInt(f[8], 10),
      snoozeMax: toInt(f[9], 3),
      vibration: f[10] === '1',
      sound: f[11] === '1',
      tuneId: toInt(f[12], 0),
      autoDismiss: toInt(f[13], 0),
      increasingVolume: f[14] === '1',
      vibeId: f.length > 15 ? toInt(f[15], 0) : 0,
      vibePreset: f.length > 16 ? toInt(f[16], 0) : 0,
    });
  });
  return out;
}
