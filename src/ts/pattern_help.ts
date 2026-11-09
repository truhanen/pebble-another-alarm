// Read-only reference data shown on the config page, taken from the watch:
// the built-in tune/vibration presets (SOUND_PRESETS / VIBE_PRESETS in
// src/c/main.c) and the long pattern-syntax help (resources/text/
// pattern_help_*.txt and cron_help.txt, the screens the watch itself shows). The config items
// carry these to the components (see config_clay.ts); the components are
// toSource()'d into the page and can't import anything. tests/
// pattern_help.test.js compares every value here against the watch's own
// source, so a change on the watch that isn't copied here fails the tests.

export interface Builtin { name: string; pattern: string; }

export const BUILTIN_TUNES: Builtin[] = [
  { name: 'Triple', pattern: 'C z C z C' },
  { name: 'Double', pattern: 'C z C' },
  { name: 'Triple slow', pattern: 'C2 z2 C2 z2 C2' },
  { name: 'Analog bell', pattern: 'C G C G C G C G' },
  { name: 'Celebration', pattern: 'C3 z C2 D6 C6 F6 E6' },
  { name: 'Lullaby', pattern: "G,8 _B,4 A,2 G,2 A,4 D4 D,6" },
  { name: 'March', pattern: 'A,,,4 z2 A,,,4 z2 A,,,4 z2 F,,,3 z2 C,, A,,,4 z2 F,,,3 z2 C,, A,,,6' },
];

// Modelled on PebbleOS's simple vibe scores (Short/Long = its standard short/long
// pulse, Double short = "Double Tap", Double long = "Double Pulse Medium") with
// their timings. The ORDER is the stored index on the watch: append only.
export const BUILTIN_VIBES: Builtin[] = [
  { name: 'Double', pattern: '2 z 2' },
  { name: 'Short', pattern: '5/2' },
  { name: 'Long', pattern: '5' },
  { name: 'Double long', pattern: '4 z2 4' },
  { name: 'Double short', pattern: '3/5 z3/5 3/5' },
];

// Order the watch's pickers (and the config page) list the presets in, as
// indices into BUILTIN_VIBES: double, double long, double short, long, short.
export const VIBE_PRESET_DISPLAY_ORDER: number[] = [0, 3, 4, 2, 1];

export const TUNE_HELP = `A tune is a list of notes and rests, separated by spaces.

Each note is an optional sharp ("^") or flat ("_") before a letter (C D E F G A B), e.g. "^C", "_D". No prefix means natural.

A note letter can have octave marks right after it: "'" for up, "," for down (e.g. "C'", "C,"). No mark means the base octave. "," can repeat up to 8 times for deeper octaves down (e.g. "C,,,"); "'" only once, up.

A duration ratio can follow: "N" for N times the default 100ms, "/D" for 1/D of it, "N/D" for both -- no ratio means the default itself (e.g. "C2" = 200ms, "C/2" = 50ms, bare "C" = 100ms).

A rest is "z" followed by the same optional ratio (e.g. "z", "z/2") -- rests are optional between notes, same as vibration.

Example pattern:

"^C' z/2 _D,"`;

export const VIBE_HELP = `A vibration pattern is a list of on-beats and rests, separated by spaces.

Each on-beat is a duration ratio: "1" means the default 100ms; "N" means N times that (e.g. "2" = 200ms); "/D" means 1/D of it (e.g. "/2" = 50ms); "N/D" combines both.

A rest is "z" followed by the same optional ratio (e.g. "z", "z/2").

Rests are optional -- two on-beats with nothing between them play back to back with no gap.

Example pattern:

"1 z/2 1"`;

export const CRON_HELP = `Alarm fires every minute that matches every cron item configured for the alarm. This applies also to weekday & day-of-month: *both* must match for an alarm to fire.

Note that if you configure an alarm for the last days of month, the firing of the alarm may be less regular due to leap days & half of months missing the 31st day. The same applies to week 53, which not every year has.

Example patterns:

Wildcard:
"*" in weekday
fires every weekday

Range:
"8-16" in hour
fires between 8-16 o'clock

List:
"1,7" in month
fires in Jan and Jul

Wildcard & step:
"*/20" in minute
fires every 20 minutes

Range & step:
"1-25/2" in week
fires every other week
between ISO weeks 1-25

Day of month + weekday:
"1-7" in day, "1" in
weekday fires on the first
Monday of every month.

Week (ISO week-of-year):
"*/2" in week fires every
other week -- lets you set
up biweekly alarms.`;
