#include "multitap_keyboard.h"

// ---- Constants --------------------------------------------------------------
#define TEXT_AREA_H  44
#define CARET_BLINK_MS 500
#define CARET_IDLE_MS  4000   // rest the caret solid-on after this long without input
#define FLASH_MS       90
#define GRID_COLS    3
#define GRID_ROWS    4
#define KEY_COUNT    (GRID_COLS * GRID_ROWS)
// Bumped from 96 for pebble-another-alarm's pattern mode: a Tune (sound)
// pattern can now run up to AC_SOUND_PATTERN_MAX_SEGMENTS (32, alarm_calc.h)
// segments of up to 13 chars each (an accidental, a note letter, up to 8
// octave markers, and a "N/D" duration ratio) plus separating spaces,
// needing up to 160 bytes (see PATTERN_KEYBOARD_MAX_LEN, main.c) --
// BUFFER_CAP must exceed the caller's own PATTERN_KEYBOARD_MAX_LEN for
// multitap_keyboard_set_max_len() to actually apply that cap (see there:
// max_bytes >= BUFFER_CAP is treated as "unlimited, just use BUFFER_CAP"),
// so this needs its own small headroom above 160, mirroring this file's
// existing "+8"/"+16" margins on other BUFFER_CAP-sized temp buffers below.
#define BUFFER_CAP   168
#define LABEL_CAP    24
#define KEY_ELEV     2     // 3D key depth: raised by this many px, sunk on press
#define GRID_VPAD    2     // white breathing room above and below the key grid

#define KEY_SHIFT 9
#define KEY_SPACE 10
#define KEY_DEL   11

typedef enum { PAGE_ALPHA = 0, PAGE_NUMBERS, PAGE_SYMBOLS, PAGE_NAV, PAGE_COUNT } Page;
typedef enum { SHIFT_OFF = 0, SHIFT_ONCE, SHIFT_LOCK } ShiftState;

// Each character key (0..8) cycles through a NULL-terminated glyph list (UTF-8).
static const char *const A0[] = { ".", ",", "?", "!", NULL };
static const char *const A1[] = { "a", "b", "c", NULL };
static const char *const A2[] = { "d", "e", "f", NULL };
static const char *const A3[] = { "g", "h", "i", NULL };
static const char *const A4[] = { "j", "k", "l", NULL };
static const char *const A5[] = { "m", "n", "o", NULL };
static const char *const A6[] = { "p", "q", "r", "s", NULL };
static const char *const A7[] = { "t", "u", "v", NULL };
static const char *const A8[] = { "w", "x", "y", "z", NULL };

static const char *const N0[] = { "1", NULL };  static const char *const N1[] = { "2", NULL };
static const char *const N2[] = { "3", NULL };  static const char *const N3[] = { "4", NULL };
static const char *const N4[] = { "5", NULL };  static const char *const N5[] = { "6", NULL };
static const char *const N6[] = { "7", NULL };  static const char *const N7[] = { "8", NULL };
static const char *const N8[] = { "9", NULL };

static const char *const S0[] = { ".", ",", "?", "!", NULL };
static const char *const S1[] = { ":", ";", "'", "\"", NULL };
static const char *const S2[] = { "@", "#", "$", NULL };
static const char *const S3[] = { "%", "&", "*", NULL };
static const char *const S4[] = { "+", "=", "-", NULL };
static const char *const S5[] = { "_", "/", "\\", NULL };
static const char *const S6[] = { "(", ")", "[", "]", NULL };
static const char *const S7[] = { "{", "}", "<", ">", NULL };
static const char *const S8[] = { "|", "^", "~", "`", NULL };

// PAGE_NAV (the 4th Page value) has no tap-cycling glyphs at all -- every key
// is empty except 3/5 (cursor left/right), which are drawn as arrow icons and
// dispatched directly in prv_press_char_key. Deliberately NOT given its own
// row in PAGE_KEY below (which stays sized for the original 3 real pages) --
// prv_eff_glyphs special-cases `page == PAGE_NAV` directly instead, avoiding
// a whole extra 9-pointer row of nothing but repeated NULL-glyph tables.
static const char *const *const PAGE_KEY[3][9] = {
  { A0, A1, A2, A3, A4, A5, A6, A7, A8 },
  { N0, N1, N2, N3, N4, N5, N6, N7, N8 },
  { S0, S1, S2, S3, S4, S5, S6, S7, S8 },
};
static const char *const PAGE_IND[PAGE_COUNT] = { "ABC", "123", "#+=", "NAV" };

// Fixed 4-symbol cycle for the bottom-left key in numeric_mode (see
// multitap_keyboard_set_numeric_mode) — covers every token
// ac_cron_parse_field-style cron fields need: "*" (wildcard), "-" (range),
// "/" (step), "," (list).
static const char *const CRON_SYMS[] = { "*", "-", "/", "," };
#define CRON_SYM_COUNT 4

// Bottom-left key cycle for pattern_mode (see multitap_keyboard_set_pattern_mode)
// -- just the fraction bar "/" for a duration ratio, for BOTH SOUND and
// VIBE now (they used to differ: SOUND's own cycle also carried octave
// markers/accidentals here, but those moved to SOUND's own key 7 -- see
// PATTERN_KEY_SOUND's own doc comment below -- once they had a real
// character key of their own to live on instead of sharing this one with
// "/"). This key's glyph source is chosen before this function ever looks
// at which of SOUND's two pages is showing, so "/" stays available
// identically on both.
static const char *const PATTERN_SYMS[] = { "/" };
#define PATTERN_SYM_COUNT 1

// Per-character-key (0-8) glyph lists for pattern mode, selected by
// multitap_keyboard_set_pattern_mode's `notes_enabled` instead of by page --
// this is a THIRD glyph source alongside PAGE_KEY/CRON_SYMS, not a variant
// of PAGE_NUMBERS's plain 1-per-key digits.
//
// SOUND's PRIMARY page ("note layout") is a plain 3x3 grid (keys 0-8,
// row-major: 0,1,2 / 3,4,5 / 6,7,8): the 7 note letters C-G/A/B one per key,
// the rest marker 'z' LAST on key 8, and key 7 (bottom-center of this grid,
// directly above the Space key) cycling the octave markers/accidentals
// instead of sitting empty -- "'"/"," are real ABC-notation octave markers
// written right after a sound note letter ("C'" up one octave, "C," down
// one octave -- see AC_NOTE_OCTAVE_*, alarm_calc.h); "^"/"_" are real
// ABC-notation sharp/flat accidentals written right BEFORE the letter
// instead (see AC_NOTE_ACCIDENTAL_*, alarm_calc.h). These used to share the
// bottom-left (Shift) key's cycle with "/" before key 7 was free for them to
// move to; the bottom-left key now only ever cycles "/" (see PATTERN_SYMS
// above), for both SOUND and VIBE alike.
//
// Duration digits are deliberately NOT on this grid at all -- they don't
// need a dedicated key, since they're already reachable two other ways: long-
// pressing ANY key (including key 7) inserts its keypad digit
// (multitap_keyboard_handle_hold, unchanged, pure key+1 arithmetic regardless
// of a key's tap-time glyph list), and pressing DOWN cycles to a second,
// SOUND-only "numbers" page (PATTERN_KEY_SOUND_NUMBERS below). 'z' stays on
// key 8 to match VIBE's own grid below, so it never moves depending on which
// mode is active. Key 7's octave/accidental cycle is note-layout-only -- the
// numbers page keeps its own key 7 as a plain "8" (see
// PATTERN_KEY_SOUND_NUMBERS below), since that page already has real digit
// content there.
//
// VIBE's grid is a deliberately different shape, not a smaller version of
// SOUND's -- vibe has no letters to make room for, so every one of the 9
// keys gets exactly one glyph, no cycling at all: digits 1-8 in row-major
// order, then 'z' last on key 8 (matching SOUND's own 'z' position above).
// A plain tap always types the intended character directly.
//
// Both SOUND and VIBE also cycle to a "navigation" sub-page via DOWN
// (SOUND: notes -> numbers -> nav; VIBE: digits -> nav), whose keys 3/5 move
// the text cursor left/right instead of typing anything -- see
// prv_locked_page_count/prv_on_nav (the plain/label keyboard's own
// equivalent, a real 4th Page value, PAGE_NAV).
static const char *const PATTERN_KEY_EMPTY[] = { NULL };   // the numbers page's own empty key 8 below
static const char *const PK_SOUND_0[] = { "C", NULL };
static const char *const PK_SOUND_1[] = { "D", NULL };
static const char *const PK_SOUND_2[] = { "E", NULL };
static const char *const PK_SOUND_3[] = { "F", NULL };
static const char *const PK_SOUND_4[] = { "G", NULL };
static const char *const PK_SOUND_5[] = { "A", NULL };
static const char *const PK_SOUND_6[] = { "B", NULL };
static const char *const PK_SOUND_7[] = { "'", ",", "^", "_", NULL };
static const char *const PK_SOUND_8[] = { "z", NULL };
static const char *const *const PATTERN_KEY_SOUND[9] = {
  PK_SOUND_0, PK_SOUND_1, PK_SOUND_2, PK_SOUND_3, PK_SOUND_4, PK_SOUND_5, PK_SOUND_6, PK_SOUND_7, PK_SOUND_8,
};
// SOUND's SECONDARY page ("numbers layout"), reached by pressing DOWN (see
// prv_cycle_page) -- reuses N0..N7 (defined above for PAGE_KEY[PAGE_NUMBERS])
// directly rather than a new set of literals, since they're already exactly
// the single-glyph "1".."8" this needs. Key 8 is deliberately left empty
// rather than showing "9": AC_NOTE_RATIO_DIGITS (alarm_calc.h) caps ratio
// digits at 1-8, and this file's own PATTERN_KEY_SOUND has always kept
// invalid digits off its tap-discoverable grids on principle (see that
// table's own history) -- long-press-anywhere already reaches "9" as an
// accepted low-discoverability edge case, which isn't license to also put it
// on this primary, labeled page.
static const char *const *const PATTERN_KEY_SOUND_NUMBERS[9] = {
  N0, N1, N2, N3, N4, N5, N6, N7, PATTERN_KEY_EMPTY,
};
static const char *const PK_VIBE_0[] = { "1", NULL };
static const char *const PK_VIBE_1[] = { "2", NULL };
static const char *const PK_VIBE_2[] = { "3", NULL };
static const char *const PK_VIBE_3[] = { "4", NULL };
static const char *const PK_VIBE_4[] = { "5", NULL };
static const char *const PK_VIBE_5[] = { "6", NULL };
static const char *const PK_VIBE_6[] = { "7", NULL };
static const char *const PK_VIBE_7[] = { "8", NULL };
static const char *const PK_VIBE_8[] = { "z", NULL };
static const char *const *const PATTERN_KEY_VIBE[9] = {
  PK_VIBE_0, PK_VIBE_1, PK_VIBE_2, PK_VIBE_3, PK_VIBE_4, PK_VIBE_5, PK_VIBE_6, PK_VIBE_7, PK_VIBE_8,
};

// Font ladder used by prv_draw_text_fitted to pick the loosest font/line
// count that fits the text field. See that function's own doc comment for
// what each level means.
static const struct { const char *key; int16_t rise; } LADDER[] = {
  { FONT_KEY_GOTHIC_28_BOLD, 5 },
  { FONT_KEY_GOTHIC_24_BOLD, 4 },
  { FONT_KEY_GOTHIC_18_BOLD, 3 },
  { FONT_KEY_GOTHIC_14_BOLD, 2 },
};
#define LADDER_N (int)(sizeof(LADDER) / sizeof(LADDER[0]))

// Toggleable extended (non-ASCII) characters. Each is appended to a base key's
// cycle when enabled, and hidden from the key labels. All enabled by default.
// To extend the set, add entries here (glyph + which alpha key 0..8 it joins)
// and the case mapping in prv_shifted(), then bump EXT_COUNT.
typedef struct { const char *glyph; int key; } ExtCharDef;
#define EXT_COUNT 5
#define MAX_EFF   16
static const ExtCharDef EXT_DEFS[EXT_COUNT] = {
  { "å", 1 }, { "ä", 1 }, { "æ", 1 }, { "ö", 5 }, { "ø", 5 },
};

// Keyboard color themes. Each holds ARGB8 values (built into GColor at draw
// time): screen background, foreground (text, divider, labels), the soft resting
// fill of a letter key, the accent fill for active/pending keys and the function
// row, the text color drawn on that accent, and a danger TRIPLE for the DEL key —
// `danger` fills the key, `danger_light` inks its delete glyph (a light tint that
// reads on the fill), and `danger_darker` is the shadow it casts / the shade it
// sinks to on press. Modeled on the throw grid (the turn-result screen): a field
// of soft filled keys plus one bold accent strip, the destructive DEL key set
// apart as a solid danger button. Keep the triple a light->dark ramp.
typedef struct {
  const char *name;
  uint8_t bg, fg, key, accent, accent_text, danger, danger_light, danger_darker;
} Theme;

// The DEL key uses Mölkky's backspace ramp (DarkCandyAppleRed fill / Melon glyph
// / BulgarianRose shadow) across every theme — the same triple the "Mölkky" app
// skin gets from the ui lib's danger default — so the destructive key reads the
// same no matter which theme is picked. Light and Dark take Pebble's default
// accent (CobaltBlue); accent_text stays white so it reads on that blue.
static const Theme THEMES[] = {
  { "Light", GColorWhiteARGB8,      GColorBlackARGB8, GColorLightGrayARGB8, GColorCobaltBlueARGB8,    GColorWhiteARGB8, GColorDarkCandyAppleRedARGB8, GColorMelonARGB8, GColorBulgarianRoseARGB8 },
  { "Dark",  GColorBlackARGB8,      GColorWhiteARGB8, GColorDarkGrayARGB8,  GColorCobaltBlueARGB8,    GColorWhiteARGB8, GColorDarkCandyAppleRedARGB8, GColorMelonARGB8, GColorBulgarianRoseARGB8 },
  { "Ocean", GColorOxfordBlueARGB8, GColorWhiteARGB8, GColorDukeBlueARGB8,  GColorVividCeruleanARGB8, GColorBlackARGB8, GColorDarkCandyAppleRedARGB8, GColorMelonARGB8, GColorBulgarianRoseARGB8 },
  { "Mint",  GColorWhiteARGB8,      GColorBlackARGB8, GColorLightGrayARGB8, GColorIslamicGreenARGB8,  GColorWhiteARGB8, GColorDarkCandyAppleRedARGB8, GColorMelonARGB8, GColorBulgarianRoseARGB8 },
  { "Amber", GColorBlackARGB8,      GColorWhiteARGB8, GColorDarkGrayARGB8,  GColorChromeYellowARGB8,  GColorBlackARGB8, GColorDarkCandyAppleRedARGB8, GColorMelonARGB8, GColorBulgarianRoseARGB8 },
};
#define THEME_COUNT (int)(sizeof(THEMES) / sizeof(THEMES[0]))

// Optional app-supplied theme: a single shared slot, registered at runtime via
// multitap_keyboard_set_app_theme() and addressed as theme index THEME_COUNT (one
// past the built-ins). Lets a host app brand the keyboard from its shared
// UiTheme without editing this lib; set_app_theme folds that palette into this
// ARGB8 Theme so the draw path treats it exactly like a built-in.
static Theme s_app_theme;
static bool  s_has_app_theme;

struct MultitapKeyboard {
  Layer *layer;
  MultitapKeyboardDoneHandler done_handler;
  void *context;

  char buffer[BUFFER_CAP];
  int  max_len;             // byte cap on buffer (0 = only BUFFER_CAP limits)
  int  cursor_pos;          // byte offset into buffer, always in [0, strlen(buffer)];
                            // where the NEXT typed character lands / what DEL
                            // removes. Moved by the "navigation" page's left/
                            // right keys (see prv_move_cursor) -- every other
                            // mutating path (typing, backspace, autocap) now
                            // operates relative to this instead of always the
                            // tail, degenerating to today's append-only
                            // behavior whenever it equals strlen(buffer).
  int  page;
  int  shift;               // ShiftState (explicit, user-driven)
  bool numeric_mode;        // fixed to PAGE_NUMBERS; bottom-left key cycles
                            // */-/,/  instead of Shift/layout-cycle. Set via
                            // multitap_keyboard_set_numeric_mode().
  bool pattern_mode;        // `page` fixed to PAGE_NUMBERS; bottom-left key
                            // cycles PATTERN_SYMS and the space key types a
                            // literal space (not "0") -- see
                            // multitap_keyboard_set_pattern_mode().
  bool pattern_notes_enabled; // pattern_mode only: character keys 0-8 pull
                            // from PATTERN_KEY_SOUND/_NUMBERS (SOUND) instead
                            // of PATTERN_KEY_VIBE (duration digit only, no
                            // pitch) -- set alongside pattern_mode,
                            // meaningless otherwise.
  int  locked_sub_page;     // numeric_mode/pattern_mode only (mutually
                            // exclusive per keyboard instance, so they safely
                            // share this one field): which sub-page of that
                            // locked mode's own DOWN-driven cycle is showing.
                            // numeric_mode: 0=digits, 1=nav. pattern_mode+VIBE:
                            // 0=digits, 1=nav. pattern_mode+SOUND: 0=notes,
                            // 1=numbers, 2=nav -- see prv_locked_page_count/
                            // prv_on_nav. Deliberately a separate field rather
                            // than reusing `page` itself for this -- toggling
                            // `page` between PAGE_NUMBERS/PAGE_ALPHA would
                            // reawaken prv_eff_glyphs's extended-character
                            // block (page==PAGE_ALPHA), which has no
                            // business firing in a locked mode.

  bool pending_active;
  int  pending_key;
  int  cycle_index;
  AppTimer *commit_timer;

  MultitapSettings settings;
  bool autocap_armed;       // derived: are we at a sentence start?
  bool autocap_suppressed;  // user pressed Shift to cancel the auto-capital
  bool ext_enabled[EXT_COUNT];
  int  theme;

  // Sticky text-field layout. prv_draw_text_fitted picks the loosest font/line
  // count that fits, but a wide pending glyph can force a wrap that a later,
  // narrower cycle glyph would undo — making the field flicker between one and
  // two lines mid-multitap. wrap_floor remembers the tightest level we've shown
  // so the layout never relaxes while the text only grows; it's reset when the
  // text shrinks (a delete), tracked via last_text_len.
  int  wrap_floor;
  int  last_text_len;
  int  last_display_level;  // ladder level prv_draw_text_fitted last drew at
                            // (single-line/wrap/front-truncate) -- a dedicated
                            // fact for the tap-to-position hit-test to consult
                            // without re-running layout, decoupled from
                            // wrap_floor's own hysteresis contract.

  bool caret_on;            // blinking caret visibility
  int  caret_idle;          // ms since last edit; blink rests on past CARET_IDLE_MS
  AppTimer *caret_timer;
  int  flash_key;           // cell to briefly highlight on tap (-1 = none)
  AppTimer *flash_timer;
  int  touch_key;           // cell currently held by the finger (-1 = none)

  GBitmap *icon_shift[3];   // indexed by ShiftState: off / once / lock
};

// ---- Case helpers -----------------------------------------------------------

static const char *prv_shifted(const char *g) {
  if (g[0] >= 'a' && g[0] <= 'z' && g[1] == '\0') {
    static char up[2]; up[0] = (char)(g[0] - 32); up[1] = '\0'; return up;
  }
  if (strcmp(g, "å") == 0) return "Å";
  if (strcmp(g, "ä") == 0) return "Ä";
  if (strcmp(g, "ö") == 0) return "Ö";
  if (strcmp(g, "æ") == 0) return "Æ";
  if (strcmp(g, "ø") == 0) return "Ø";
  return g;
}
static bool prv_has_case(const char *g) { return strcmp(prv_shifted(g), g) != 0; }

// Shift state actually in effect = explicit shift, or auto-cap when armed.
// Always SHIFT_OFF in pattern_mode -- pattern mode's own glyph tables are
// case-invariant EXCEPT the rest marker 'z' (PK_SOUND_7/PK_VIBE_8), which
// must stay lowercase (it's a fixed grammar symbol, not a real letter to
// capitalize) -- auto-caps arming at the start of a fresh entry would
// otherwise silently commit an uppercase 'Z' that ac_vibe_pattern_parse/
// ac_sound_pattern_parse both reject. This is the same class of bug the
// pattern-mode octave-duplicate keys used to guard against before they
// were removed in favor of the bottom-left "'"/"," markers (see that
// history in CLAUDE.md) -- reintroducing a real lowercase glyph
// (z) needs this guard back. numeric_mode is included too for the same
// reason in principle, even though CRON_SYMS/digits are already
// case-invariant so it's currently a no-op there.
static int prv_effective_shift(MultitapKeyboard *kb) {
  if (kb->pattern_mode || kb->numeric_mode) return SHIFT_OFF;
  if (kb->shift == SHIFT_LOCK) return SHIFT_LOCK;
  if (kb->shift == SHIFT_ONCE) return SHIFT_ONCE;
  if (kb->settings.auto_caps && kb->autocap_armed && !kb->autocap_suppressed)
    return SHIFT_ONCE;
  return SHIFT_OFF;
}

// Render glyph `g` in the case for an explicit shift state (for drawing key
// labels in a case that may differ from the in-effect shift — see prv_next_shift).
static void prv_render_glyph_cased(const char *g, int shift, char *out, size_t cap) {
  const char *src = (shift != SHIFT_OFF) ? prv_shifted(g) : g;
  strncpy(out, src, cap - 1); out[cap - 1] = '\0';
}

static void prv_render_glyph(MultitapKeyboard *kb, const char *g, char *out, size_t cap) {
  prv_render_glyph_cased(g, prv_effective_shift(kb), out, cap);
}

// The shift that applies to the NEXT new letter, for drawing keys OTHER than the
// active one. A pending multitap glyph has already claimed the in-effect shift —
// its case is locked while it keeps cycling — so a ONCE (explicit or auto-cap) is
// spent on it and only LOCK persists. With nothing pending this is just the
// in-effect shift, so resting keys are unchanged. This lets the pressed key stay
// capital ("ABC") while the rest drop to lowercase ("def") and Shift reads off.
static int prv_next_shift(MultitapKeyboard *kb) {
  if (!kb->pending_active) return prv_effective_shift(kb);
  return (kb->shift == SHIFT_LOCK) ? SHIFT_LOCK : SHIFT_OFF;
}

// numeric_mode/pattern_mode are mutually exclusive per keyboard instance, so
// they safely share locked_sub_page for their own DOWN-driven sub-page cycle
// (see that field's own doc comment). Returns how many sub-pages the CURRENT
// locked mode has, or 0 if not in a locked mode at all (the plain/label
// keyboard instead cycles a real PAGE_NAV via the ordinary Page enum).
static int prv_locked_page_count(MultitapKeyboard *kb) {
  if (kb->numeric_mode) return 2;                                 // digits, nav
  if (kb->pattern_mode) return kb->pattern_notes_enabled ? 3 : 2;  // notes(+numbers), nav
  return 0;
}

// True when the keyboard is currently showing the navigation sub-page/page --
// nav is always the LAST sub-page of whichever cycle applies above, or
// PAGE_NAV itself for the plain/label keyboard.
static bool prv_on_nav(MultitapKeyboard *kb) {
  int count = prv_locked_page_count(kb);
  if (count > 0) return kb->locked_sub_page == count - 1;
  return kb->page == PAGE_NAV;
}

// Effective glyph list for (page,key): base glyphs plus any enabled extended
// characters assigned to this key (ALPHA only). Writes pointers into `out`.
static int prv_eff_glyphs(MultitapKeyboard *kb, int page, int key,
                          const char **out, int cap) {
  int n = 0;
  if (key == KEY_SHIFT && kb->pattern_mode) {
    for (int j = 0; j < PATTERN_SYM_COUNT && n < cap; j++) out[n++] = PATTERN_SYMS[j];
    return n;
  }
  if (key == KEY_SHIFT && kb->numeric_mode) {
    for (int j = 0; j < CRON_SYM_COUNT && n < cap; j++) out[n++] = CRON_SYMS[j];
    return n;
  }
  // Pattern mode's character keys (0-8) pull from their own glyph tables
  // instead of PAGE_KEY -- `key` is guaranteed 0-8 here: KEY_SHIFT already
  // returned above, and callers never pass KEY_SPACE/KEY_DEL to this
  // function. SOUND additionally picks between its two real pages via
  // locked_sub_page (see that field's own doc comment) -- deliberately NOT
  // via the `page` param itself, which stays pinned to PAGE_NUMBERS for
  // pattern mode's whole lifetime (see multitap_keyboard_set_pattern_mode),
  // so the `page == PAGE_ALPHA` extended-character check below can never
  // fire during pattern mode regardless of which sub-page is showing. Both
  // pattern_mode and numeric_mode's own nav sub-page (prv_on_nav) return no
  // glyphs at all here -- keys 3/5 are special-cased directly in
  // prv_press_char_key/the draw loop instead of through this table.
  bool on_nav = prv_on_nav(kb);
  const char *const *base;
  if (on_nav && (kb->pattern_mode || kb->numeric_mode)) {
    base = PATTERN_KEY_EMPTY;
  } else if (kb->pattern_mode) {
    base = kb->pattern_notes_enabled
             ? (kb->locked_sub_page == 1 ? PATTERN_KEY_SOUND_NUMBERS[key] : PATTERN_KEY_SOUND[key])
             : PATTERN_KEY_VIBE[key];
  } else if (page == PAGE_NAV) {
    base = PATTERN_KEY_EMPTY;
  } else {
    base = PAGE_KEY[page][key];
  }
  for (int j = 0; base[j] && n < cap; j++) out[n++] = base[j];
  if (page == PAGE_ALPHA) {
    for (int e = 0; e < EXT_COUNT && n < cap; e++) {
      if (kb->ext_enabled[e] && EXT_DEFS[e].key == key) out[n++] = EXT_DEFS[e].glyph;
    }
  }
  return n;
}

// ---- Buffer + sentence helpers ----------------------------------------------

static void prv_recompute_autocap(MultitapKeyboard *kb);   // defined below

// Inserts at kb->cursor_pos (not always the tail) -- degenerates to today's
// exact append-only behavior whenever cursor_pos == strlen(buffer), true for
// every keyboard that never visits the navigation page/taps the text field.
static void prv_append(MultitapKeyboard *kb, const char *s) {
  size_t l = strlen(kb->buffer), sl = strlen(s);
  if (l + sl >= BUFFER_CAP) return;
  if (kb->max_len > 0 && l + sl > (size_t)kb->max_len) return;  // would overflow cap
  int pos = kb->cursor_pos;
  if (pos < 0) pos = 0;
  if (pos > (int)l) pos = (int)l;
  memmove(kb->buffer + pos + sl, kb->buffer + pos, l - (size_t)pos + 1);  // shift tail right, incl NUL
  memcpy(kb->buffer + pos, s, sl);
  kb->cursor_pos = pos + (int)sl;
}

// Deletes the UTF-8 glyph immediately BEFORE cursor_pos (not always the last
// glyph in the buffer) and moves cursor_pos back to the deleted glyph's start.
static void prv_utf8_backspace(MultitapKeyboard *kb) {
  int pos = kb->cursor_pos;
  if (pos <= 0) return;
  int start = pos - 1;
  while (start > 0 && ((unsigned char)kb->buffer[start] & 0xC0) == 0x80) start--;
  size_t len = strlen(kb->buffer);
  memmove(kb->buffer + start, kb->buffer + pos, len - (size_t)pos + 1);
  kb->cursor_pos = start;
}

static void prv_do_delete(MultitapKeyboard *kb) {
  if (kb->pending_active) { kb->pending_active = false; return; }
  prv_utf8_backspace(kb);
  prv_recompute_autocap(kb);
}

// Moves cursor_pos one UTF-8 glyph left/right, clamped to the buffer -- the
// navigation page's key 3/5 action (see prv_press_char_key) and a tap on the
// text field both funnel through this.
static void prv_move_cursor(MultitapKeyboard *kb, int dir) {
  int len = (int)strlen(kb->buffer);
  int pos = kb->cursor_pos;
  if (dir < 0) {
    if (pos <= 0) return;
    pos--;
    while (pos > 0 && ((unsigned char)kb->buffer[pos] & 0xC0) == 0x80) pos--;
  } else {
    if (pos >= len) return;
    pos++;
    while (pos < len && ((unsigned char)kb->buffer[pos] & 0xC0) == 0x80) pos++;
  }
  kb->cursor_pos = pos;
  prv_recompute_autocap(kb);
}

// True when `cursor_pos` sits at the start of a new sentence -- looks only at
// buf[0..cursor_pos), i.e. what's actually before the cursor, not always the
// buffer's last byte (that generalization matters once the cursor can sit
// mid-string: typing after moving left should react to what precedes THAT
// position, not stale content further along the buffer).
static bool prv_at_sentence_start(const char *buf, int cursor_pos) {
  int len = cursor_pos;
  if (len <= 0) return true;
  if (buf[len - 1] == '\n') return true;        // start of a fresh line
  if (buf[len - 1] != ' ') return false;        // mid-word, not a fresh start
  int i = len - 1;
  while (i >= 0 && buf[i] == ' ') i--;
  if (i < 0) return true;                        // only spaces so far
  char c = buf[i];
  return (c == '.' || c == '!' || c == '?' || c == '\n');
}

// Recompute auto-cap arming from the buffer; clear any one-letter suppression.
static void prv_recompute_autocap(MultitapKeyboard *kb) {
  kb->autocap_armed = prv_at_sentence_start(kb->buffer, kb->cursor_pos);
  kb->autocap_suppressed = false;
}

// ---- Commit / multi-tap -----------------------------------------------------

static void prv_emit_glyph(MultitapKeyboard *kb, const char *g) {
  char out[8];
  prv_render_glyph(kb, g, out, sizeof(out));
  bool cased = prv_has_case(g);
  prv_append(kb, out);
  if (cased && kb->shift == SHIFT_ONCE) kb->shift = SHIFT_OFF;  // spend explicit once
  prv_recompute_autocap(kb);
}

static void prv_commit_pending(MultitapKeyboard *kb) {
  if (!kb->pending_active) return;
  const char *eff[MAX_EFF];
  int n = prv_eff_glyphs(kb, kb->page, kb->pending_key, eff, MAX_EFF);
  if (kb->cycle_index >= n) kb->cycle_index = 0;
  kb->pending_active = false;
  if (n > 0) prv_emit_glyph(kb, eff[kb->cycle_index]);
}

static void prv_commit_timer_cb(void *data) {
  MultitapKeyboard *kb = (MultitapKeyboard *)data;
  kb->commit_timer = NULL;
  prv_commit_pending(kb);
  layer_mark_dirty(kb->layer);
}

static void prv_arm_commit_timer(MultitapKeyboard *kb) {
  int ms = kb->settings.commit_timeout_ms;
  if (ms < 150) ms = 150;
  if (kb->commit_timer) {
    if (app_timer_reschedule(kb->commit_timer, ms)) return;
    kb->commit_timer = NULL;
  }
  kb->commit_timer = app_timer_register(ms, prv_commit_timer_cb, kb);
}

// numeric_mode and pattern_mode both cycle their own small locked_sub_page
// range (digits/notes(+numbers) -> nav -> back), via prv_locked_page_count;
// the plain/label keyboard instead cycles the real Page enum, now 4-deep
// (ALPHA/NUMBERS/SYMBOLS/NAV).
static void prv_cycle_page(MultitapKeyboard *kb) {
  int count = prv_locked_page_count(kb);
  if (count > 0) {
    prv_commit_pending(kb);
    kb->locked_sub_page = (kb->locked_sub_page + 1) % count;
    layer_mark_dirty(kb->layer);
    return;
  }
  prv_commit_pending(kb);
  kb->page = (kb->page + 1) % PAGE_COUNT;
  layer_mark_dirty(kb->layer);
}

// ---- Feedback (blinking caret, tap flash) ------------------------------------

static void prv_caret_timer_cb(void *data) {
  MultitapKeyboard *kb = (MultitapKeyboard *)data;
  kb->caret_idle += CARET_BLINK_MS;
  if (kb->caret_idle >= CARET_IDLE_MS) {     // idle: stop the 2 Hz redraw, rest visible
    kb->caret_timer = NULL;
    if (!kb->caret_on) { kb->caret_on = true; layer_mark_dirty(kb->layer); }
    return;
  }
  kb->caret_on = !kb->caret_on;
  kb->caret_timer = app_timer_register(CARET_BLINK_MS, prv_caret_timer_cb, kb);
  layer_mark_dirty(kb->layer);
}

// Any edit wakes the caret: solid-on, idle counter reset, blink restarted from
// scratch. The cursor stays steady while typing (or while a navigation-page
// key repeats on hold), then blinks during a brief pause, then stops
// redrawing the grid entirely once the user walks away. The blink timer is
// unconditionally canceled and re-armed here (not just started `if` none was
// running) -- a wake that left an already-ticking timer on its OLD schedule
// could still fire soon after (toggling the caret back off) despite
// continuous activity, since that timer's own next-fire time was set before
// this wake happened at all; confirmed as a real, reported bug during
// continuous nav-key hold-to-repeat (each repeat tick is ~100ms, well under
// CARET_BLINK_MS's 500ms, so the caret should never actually reach a toggle
// while repeating).
static void prv_caret_wake(MultitapKeyboard *kb) {
  kb->caret_idle = 0;
  kb->caret_on = true;
  if (kb->caret_timer) { app_timer_cancel(kb->caret_timer); kb->caret_timer = NULL; }
  kb->caret_timer = app_timer_register(CARET_BLINK_MS, prv_caret_timer_cb, kb);
}

static void prv_flash_timer_cb(void *data) {
  MultitapKeyboard *kb = (MultitapKeyboard *)data;
  kb->flash_timer = NULL;
  kb->flash_key = -1;
  layer_mark_dirty(kb->layer);
}

static void prv_flash(MultitapKeyboard *kb, int key) {
  kb->flash_key = key;
  if (kb->flash_timer) app_timer_cancel(kb->flash_timer);
  kb->flash_timer = app_timer_register(FLASH_MS, prv_flash_timer_cb, kb);
}

static void prv_press_char_key(MultitapKeyboard *kb, int key) {
  // Navigation page/sub-page: keys 3 (middle-row-left) and 5 (middle-row-
  // right) move the cursor instead of cycling/committing a glyph -- a
  // one-shot action per tap, like Space/DEL, not a multi-tap cycle. Every
  // other key on this page is empty (see prv_eff_glyphs) and falls through
  // to the `n == 0` no-op below.
  if (prv_on_nav(kb) && (key == 3 || key == 5)) {
    prv_move_cursor(kb, key == 3 ? -1 : +1);
    return;
  }
  const char *eff[MAX_EFF];
  int n = prv_eff_glyphs(kb, kb->page, key, eff, MAX_EFF);
  if (n == 0) return;
  if (n == 1) { prv_commit_pending(kb); prv_emit_glyph(kb, eff[0]); return; }

  if (kb->pending_active && kb->pending_key == key) {
    kb->cycle_index = (kb->cycle_index + 1) % n;   // re-press: advance the glyph in place
    prv_arm_commit_timer(kb);
    return;
  }
  prv_commit_pending(kb);
  // At the length cap a new key can't start a fresh char: a pending here would
  // preview (then silently drop) an over-length glyph, which reads as the last
  // letter being replaced. Cycling the active key above is still fine — it only
  // swaps the pending char in place and never grows the text.
  if (kb->max_len > 0 && (int)strlen(kb->buffer) >= kb->max_len) return;
  kb->pending_key = key; kb->cycle_index = 0; kb->pending_active = true;
  prv_arm_commit_timer(kb);
}

static void prv_do_space(MultitapKeyboard *kb) {
  prv_commit_pending(kb);
  // Numeric mode repurposes the space key as digit "0" (there's no
  // dedicated 0 key among the 9 character keys); pattern mode instead needs
  // a real space to separate tokens -- so it must NOT inherit this.
  if (kb->page == PAGE_NUMBERS && !kb->pattern_mode) { prv_append(kb, "0"); prv_recompute_autocap(kb); return; }

  prv_append(kb, " ");
  prv_recompute_autocap(kb);
}

static void prv_press_key(MultitapKeyboard *kb, int key) {
  if (key < 0 || key >= KEY_COUNT) return;
  prv_caret_wake(kb);

  if (key == KEY_SHIFT) {
    if (kb->numeric_mode || kb->pattern_mode) {
      prv_press_char_key(kb, KEY_SHIFT);   // cycles */-/,/ (numeric) or types "-" (pattern) like any other key
    } else if (kb->page != PAGE_ALPHA) {
      prv_cycle_page(kb);                  // doubles as the layout-cycle button
    } else if (kb->shift == SHIFT_OFF && kb->settings.auto_caps &&
               kb->autocap_armed && !kb->autocap_suppressed) {
      kb->autocap_suppressed = true;       // first press cancels an auto-capital
    } else {
      kb->shift = (kb->shift + 1) % 3;
    }
  } else if (key == KEY_DEL) {
    prv_do_delete(kb);
  } else if (key == KEY_SPACE) {
    prv_do_space(kb);
  } else {
    prv_press_char_key(kb, key);
  }
  layer_mark_dirty(kb->layer);
}

// ---- Hit testing ------------------------------------------------------------

// The origin that centers `content` inside `box`.
static GPoint prv_center_origin(GRect box, GSize content) {
  return (GPoint){
    .x = (int16_t)(box.origin.x + (box.size.w - content.w) / 2),
    .y = (int16_t)(box.origin.y + (box.size.h - content.h) / 2),
  };
}

// Key-grid geometry shared by drawing and hit-testing: integer cell size and the
// centered top-left origin. The grid sits GRID_VPAD below the text header and the
// same distance above the screen bottom, so the keyboard has equal white margins;
// any division remainder is split evenly L/R and T/B by the centering.
static void prv_grid_metrics(GRect b, int *cw, int *ch, GPoint *origin) {
  int top = TEXT_AREA_H + GRID_VPAD;
  int avail_h = b.size.h - GRID_VPAD - top;
  int w = b.size.w / GRID_COLS;
  int h = avail_h > 0 ? avail_h / GRID_ROWS : 0;
  *cw = w; *ch = h;
  *origin = prv_center_origin(GRect(0, top, b.size.w, avail_h),
                              GSize(w * GRID_COLS, h * GRID_ROWS));
}

static int prv_key_at_point(MultitapKeyboard *kb, GPoint p) {
  GRect b = layer_get_bounds(kb->layer);
  if (p.y < TEXT_AREA_H) return -1;
  int cw, ch; GPoint go;
  prv_grid_metrics(b, &cw, &ch, &go);
  if (cw <= 0 || ch <= 0) return -1;
  int col = (p.x - go.x) / cw, row = (p.y - go.y) / ch;
  if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS) return -1;
  return row * GRID_COLS + col;
}

static int prv_prefix_w(const char *text, int off, GFont f);   // defined below, in ---- Drawing ----

// Snaps a local x position (0 == where `lo` itself would render) to the
// nearest UTF-8 character boundary within text[lo..hi), by comparing measured
// widths via prv_prefix_w — the same "nearest boundary, midpoint as the snap
// line" rule ordinary text-editor caret hit-testing uses.
static int prv_snap_offset(const char *text, int lo, int hi, int x, GFont f) {
  int base = prv_prefix_w(text, lo, f);
  int best_off = lo, best_dist = -1;
  int off = lo;
  while (1) {
    int w = prv_prefix_w(text, off, f) - base;
    int dist = (x > w) ? (x - w) : (w - x);
    if (best_dist < 0 || dist < best_dist) { best_dist = dist; best_off = off; }
    if (off >= hi) break;
    int next = off + 1;
    while (next < hi && ((unsigned char)text[next] & 0xC0) == 0x80) next++;
    off = next;
  }
  return best_off;
}

// Maps a touch point to a byte offset in kb->buffer, for placing the cursor
// there (see multitap_keyboard_handle_tap). Returns false if `p` isn't in the
// text-field strip at all (same p.y < TEXT_AREA_H cutoff prv_key_at_point
// uses for the whole field, so the two hit-tests partition the layer with no
// seam). Uses kb->last_display_level (set by the last prv_draw_text_fitted
// call) rather than re-running layout.
//
// Precise in the single-line display (measure boundary-by-boundary and snap
// to the nearest, via prv_snap_offset) and in the wrapped-two-line display (a
// small greedy word-wrap pass of our own, mirroring Pebble's built-in
// word-wrap rule, finds exactly which bytes fall on line 1 vs line 2, then
// hit-tests within that line the same way). The front-truncation display has
// no representable position for its hidden leading text at all, so it falls
// back to a pure geometric test instead of guessing: the left half of the
// field jumps the cursor to the very start, the right half to the very end.
static bool prv_text_field_hit(MultitapKeyboard *kb, GPoint p, int *offset_out) {
  if (p.y >= TEXT_AREA_H) return false;
  GRect b = layer_get_bounds(kb->layer);
  GRect field = GRect(4, 4, b.size.w - 8, TEXT_AREA_H - 8);
  GRect tr = GRect(field.origin.x + 6, field.origin.y, field.size.w - 12, field.size.h);

  const char *text = kb->buffer;
  int len = (int)strlen(text);
  int level = kb->last_display_level;

  if (level >= 2 * LADDER_N) {   // front-truncated: geometric fallback only
    int mid = tr.origin.x + tr.size.w / 2;
    *offset_out = (p.x < mid) ? 0 : len;
    return true;
  }

  int x = p.x - tr.origin.x;
  if (x < 0) x = 0;

  if (level < LADDER_N) {   // single line
    GFont f = fonts_get_system_font(LADDER[level].key);
    *offset_out = prv_snap_offset(text, 0, len, x, f);
    return true;
  }

  // Wrapped to (up to) two lines: find the line-break point ourselves via a
  // greedy word-wrap walk (break at the last space before the line would
  // overflow the box width), pick the line from the tap's y, then hit-test
  // x within that line's own span.
  int i = level - LADDER_N;
  GFont f = fonts_get_system_font(LADDER[i].key);
  int line_h = graphics_text_layout_get_content_size(
                 "Ag", f, GRect(0, 0, 2000, 200), GTextOverflowModeFill, GTextAlignmentLeft).h;
  int box_w = tr.size.w;
  int line1_end = len;
  int last_space = -1;
  int off = 0;
  while (off < len) {
    int next = off + 1;
    while (next < len && ((unsigned char)text[next] & 0xC0) == 0x80) next++;
    if (text[off] == ' ') last_space = off;
    if (prv_prefix_w(text, next, f) > box_w) {
      line1_end = (last_space >= 0) ? last_space + 1 : off;
      break;
    }
    off = next;
  }
  int on_line2 = (p.y - tr.origin.y) >= line_h;
  int lo = on_line2 ? line1_end : 0;
  int hi = on_line2 ? len : line1_end;
  *offset_out = prv_snap_offset(text, lo, hi, x, f);
  return true;
}

void multitap_keyboard_handle_tap(MultitapKeyboard *kb, GPoint point) {
  if (!kb) return;
  int offset;
  if (prv_text_field_hit(kb, point, &offset)) {
    prv_commit_pending(kb);
    kb->cursor_pos = offset;
    prv_recompute_autocap(kb);
    prv_caret_wake(kb);
    layer_mark_dirty(kb->layer);
    return;
  }
  int key = prv_key_at_point(kb, point);
  if (key < 0) return;
  prv_flash(kb, key);
  prv_press_key(kb, key);
}

// Press feedback: highlight the key under the finger from touchdown until liftoff
// (off-grid clears it). Purely visual — the typed glyph is still decided by
// handle_tap/handle_hold on release.
void multitap_keyboard_handle_touch_down(MultitapKeyboard *kb, GPoint point) {
  if (!kb) return;
  int key = prv_key_at_point(kb, point);
  if (key == kb->touch_key) return;
  kb->touch_key = key;
  layer_mark_dirty(kb->layer);
}

void multitap_keyboard_handle_touch_up(MultitapKeyboard *kb) {
  if (!kb || kb->touch_key < 0) return;
  kb->touch_key = -1;
  layer_mark_dirty(kb->layer);
}

// Cell index under `point`, or -1 off the grid. Lets the touch driver tell when a
// finger has slid off the key it started on (to cancel the tap / long-press).
int multitap_keyboard_key_at_point(MultitapKeyboard *kb, GPoint point) {
  return kb ? prv_key_at_point(kb, point) : -1;
}

// True when `point` falls on the DEL key. The touch driver uses this to give the
// on-screen DEL key the same hold-to-repeat erase as the Down button, instead of
// the one-shot hold the other keys get.
bool multitap_keyboard_point_is_delete(MultitapKeyboard *kb, GPoint point) {
  return kb && prv_key_at_point(kb, point) == KEY_DEL;
}

// 0 if `point` isn't one of the navigation page's move-cursor keys (3/5),
// -1 if it's the left key, +1 if it's the right key. The touch driver uses
// this to give these two keys the same hold-to-repeat behavior as the DEL
// key (see multitap_keyboard_point_is_delete/multitap_keyboard_move_cursor),
// instead of the one-shot hold every other key gets.
int multitap_keyboard_point_is_nav_key(MultitapKeyboard *kb, GPoint point) {
  if (!kb || !prv_on_nav(kb)) return 0;
  int key = prv_key_at_point(kb, point);
  if (key == 3) return -1;
  if (key == 5) return 1;
  return 0;
}

// Moves the cursor one UTF-8 glyph left (dir<0) or right (dir>0), waking the
// caret and redrawing -- the programmatic equivalent of tapping/holding the
// navigation page's own move-cursor keys, for the touch driver's repeat timer
// to call directly without synthesizing a fake tap point.
void multitap_keyboard_move_cursor(MultitapKeyboard *kb, int dir) {
  if (!kb) return;
  prv_caret_wake(kb);
  prv_move_cursor(kb, dir);
  layer_mark_dirty(kb->layer);
}

// Long-press: character keys insert their phone-keypad digit (key 0..8 ->
// '1'..'9'); the space/0 key inserts the OTHER of space/0 for its page (so on
// the 123 page, holding 0 types a space); the bottom-left key cycles layout.
// The navigation page's own keys 3/5 are excluded from the digit-insert (they
// have no glyph to begin with, but key+1 digit-insert is otherwise generic
// and keyed off nothing but the raw key index) -- the touch driver never
// actually reaches this function for those two keys at all (it intercepts
// them via multitap_keyboard_point_is_nav_key for hold-to-repeat instead, see
// multitap_keyboard_window.c), but guarding here too keeps this function
// correct on its own regardless of caller.
void multitap_keyboard_handle_hold(MultitapKeyboard *kb, GPoint point) {
  if (!kb) return;
  int key = prv_key_at_point(kb, point);
  if (key < 0) return;
  if (prv_on_nav(kb) && (key == 3 || key == 5)) return;
  prv_caret_wake(kb);
  prv_flash(kb, key);
  if (key == KEY_SHIFT) { prv_cycle_page(kb); return; }
  if (key == KEY_DEL)   { prv_press_key(kb, KEY_DEL); return; }
  prv_commit_pending(kb);
  if (key == KEY_SPACE) prv_append(kb, (kb->page == PAGE_NUMBERS && !kb->pattern_mode) ? " " : "0");
  else { char d[2] = { (char)('1' + key), '\0' }; prv_append(kb, d); }
  prv_recompute_autocap(kb);
  layer_mark_dirty(kb->layer);
}

// ---- Drawing ----------------------------------------------------------------

// Build the label for key `i`, drawing its glyphs in case `shift`. Callers pass
// the in-effect shift for the active multitap key and prv_next_shift() for the
// rest, so a pressed key stays capital while the others fall back to lowercase.
static void prv_key_label(MultitapKeyboard *kb, int i, int shift, char *out, size_t cap) {
  out[0] = '\0';
  if (i == KEY_SHIFT && !kb->numeric_mode && !kb->pattern_mode) {
    if (kb->page != PAGE_ALPHA) {                        // layout-cycle button
      strncpy(out, PAGE_IND[(kb->page + 1) % PAGE_COUNT], cap - 1);
      out[cap - 1] = '\0'; return;
    }
    const char *s = (shift == SHIFT_OFF) ? "ab" : (shift == SHIFT_ONCE) ? "Ab" : "AB";
    strncpy(out, s, cap - 1); out[cap - 1] = '\0'; return;
  }
  // numeric_mode: falls through to the generic glyph-concatenation loop
  // below, which (via prv_eff_glyphs' KEY_SHIFT special case) renders the
  // cycled "*-/," symbols exactly like any other multi-glyph key.
  if (i == KEY_DEL)   { strncpy(out, "DEL", cap - 1); out[cap - 1] = '\0'; return; }
  if (i == KEY_SPACE) { strncpy(out, (kb->page == PAGE_NUMBERS && !kb->pattern_mode) ? "0" : "space", cap - 1);
                        out[cap - 1] = '\0'; return; }
  const char *eff[MAX_EFF];
  int n = prv_eff_glyphs(kb, kb->page, i, eff, MAX_EFF);
  for (int j = 0; j < n; j++) {
    if ((unsigned char)eff[j][0] >= 0x80) continue;   // hide non-ASCII glyphs
    char gb[8];
    prv_render_glyph_cased(eff[j], shift, gb, sizeof(gb));
    if (strlen(out) + strlen(gb) < cap - 1) strcat(out, gb);
  }
}

// Rendered width of text[0..off) in font `f` — i.e. the x of that byte offset on a
// left-aligned line. Cumulative-width measurement is exact (kerning included), so
// it pinpoints where a glyph/caret lands without a per-glyph layout API.
static int prv_prefix_w(const char *text, int off, GFont f) {
  char pre[BUFFER_CAP + 8];
  if (off < 0) off = 0;
  if (off > (int)sizeof(pre) - 1) off = (int)sizeof(pre) - 1;
  memcpy(pre, text, off); pre[off] = '\0';
  return graphics_text_layout_get_content_size(
           pre, f, GRect(0, 0, 2000, 200), GTextOverflowModeFill, GTextAlignmentLeft).w;
}

// Draw `text` left-aligned in `box`, picking the loosest layout that fits, in
// increasing "tightness". The candidates form one ordered ladder:
//   levels 0..N-1   : one line, shrinking through the font ladder by width;
//   level  2N-1     : wrap to (up to) two lines, at the SMALLEST font only --
//                     the field is a fixed ~36px-tall box, which only the
//                     smallest font's two real lines can plausibly fit (every
//                     larger font in the ladder needs more than that for just
//                     ONE line elsewhere in this app); levels N..2N-2 are
//                     never produced by the selection below, though the
//                     drawing code still handles any level in N..2N-1
//                     generically, in case `wrap_floor` hysteresis ever
//                     carries one in from elsewhere;
//   level  2N       : truncate from the FRONT with a leading "..." so the most
//                     recent characters stay visible.
// Text is vertically centered.
//
// `wrap_floor` (may be NULL) adds hysteresis: the chosen level is clamped to be
// no looser than *wrap_floor, and *wrap_floor is raised to it. A tighter level
// always fits (smaller font / more lines), so clamping up never overflows. This
// stops the field from snapping one-line<->two-lines as a multitap cycles a key
// through glyphs of different widths; the caller relaxes the floor on a delete.
// `ul_start`/`ul_end` (byte offsets into `text`, or -1) mark a span to underline
// in `ul_color` — used to flag the still-cycling multi-tap glyph. `caret_at` (a
// byte offset, or -1) is where the (1-byte) caret glyph sits in `text`, drawn in
// `caret_color`; any real text after it is redrawn in `body_color` at its own
// measured position (both only matter in the single-line layout — see below).
// The caret can sit anywhere in `text` now (not just the tail), since the
// navigation page/a text-field tap can move the cursor mid-string.
//
// Interior-cursor positioning is fully supported only in the single-line
// layout, where a byte offset's x-extent can be measured exactly via
// prv_prefix_w. The wrap-to-two-lines layout draws `text` (caret glyph
// already spliced in by the caller) via Pebble's own word-wrap, so the caret
// lands in the right place for free without any positional code here. The
// front-truncation layout can't place an interior cursor at all (the
// discarded leading text has no on-screen position to redraw a cursor at),
// so this function falls back to the wrap layout instead of front-truncating
// whenever the caret has real content after it (see the `level` adjustment
// below) — front-truncation is reserved for the (still common) case where
// the cursor stays at the tail.
static void prv_draw_text_fitted(GContext *ctx, const char *text, GRect box,
                                 int *wrap_floor,
                                 int ul_start, int ul_end, GColor ul_color,
                                 int caret_at, GColor caret_color, GColor body_color,
                                 int *level_out) {
  // GOTHIC reserves generous padding ABOVE the caps (headroom for accents like
  // Ä/Ö), so centering the measured content box leaves the visible glyphs sitting
  // low. `rise` lifts them back to the optical middle; values were measured on the
  // emulator (28pt caps centered when lifted 5px) and scale with font size.
  // (LADDER itself now lives at file scope -- see its own doc comment.)
  const int N = LADDER_N;
  const GRect wide = GRect(0, 0, 2000, 200);            // single-line measure
  const GRect wrap = GRect(0, 0, box.size.w, 400);      // wrapped-to-width measure
  char buf[BUFFER_CAP + 16];   // fits "..." + the longest caller buffer, with room to spare

  // Level selection measures the text WITHOUT the trailing blinking caret
  // glyph (if any), even though the caret IS still part of `text` for
  // drawing purposes elsewhere in this function. The caret toggles on/off
  // every CARET_BLINK_MS -- if that extra glyph were allowed to affect
  // which level fits, the field would visibly reflow (e.g. one line <-> two
  // lines) in sync with the blink whenever the real, committed text sits
  // right at that boundary. `wrap_floor`'s own hysteresis below doesn't
  // fully prevent this either: it only ratchets to a TIGHTER level and
  // never relaxes back on its own (only an actual delete does), so once
  // the caret's extra glyph pushed the level tighter one blink, it could
  // stay stuck a notch tighter than the real content needs -- confirmed
  // as a real, reported bug, not a theoretical one. Measuring without the
  // caret removes the coupling entirely.
  // Excises ONLY the 1-byte caret glyph itself, not everything after it --
  // with an interior cursor, real text can follow the caret and must still
  // count toward the fit decision. An earlier version of this dropped the
  // whole tail past caret_at (fine when the cursor could only ever sit at
  // the tail, since there was no real tail left to drop) -- with the
  // navigation page/a field tap able to move the cursor mid-string, that
  // shortcut let the level selection see only a short prefix and pick a
  // looser (bigger) font than the full text actually fits, confirmed as a
  // real, reported bug: right after deleting a character mid-string,
  // wrap_floor resets to 0 (see below) and this frame's measurement (caret
  // solid on, right after the delete wakes it) would see only the prefix,
  // picking a too-loose level whose actual draw then overflowed/clipped the
  // real (longer) text for one frame, until the next caret blink's
  // off-phase measurement (caret_at==-1, no truncation at all) recomputed
  // the correct tighter level and self-corrected.
  char measure_buf[BUFFER_CAP + 16];
  const char *measure_text = text;
  if (caret_at >= 0) {
    int c = caret_at < (int)sizeof(measure_buf) ? caret_at : (int)sizeof(measure_buf) - 1;
    size_t tail_src = (size_t)c + 1;              // skip the 1-byte caret glyph itself
    size_t avail = sizeof(measure_buf) - (size_t)c - 1;
    size_t tlen = strlen(text);
    size_t tail_len = (tlen > tail_src) ? tlen - tail_src : 0;
    if (tail_len > avail) tail_len = avail;
    memcpy(measure_buf, text, c);
    memcpy(measure_buf + c, text + tail_src, tail_len);
    measure_buf[c + tail_len] = '\0';
    measure_text = measure_buf;
  }

  // Find the loosest level that fits this text on its own.
  int natural = 2 * N;   // truncate, unless something looser fits first
  for (int i = 0; i < N; i++) {
    GFont f = fonts_get_system_font(LADDER[i].key);
    GSize sz = graphics_text_layout_get_content_size(
                 measure_text, f, wide, GTextOverflowModeFill, GTextAlignmentLeft);
    if (sz.w <= box.size.w) { natural = i; break; }
  }
  // The text field is a fixed ~36px-tall box -- comfortably one line at
  // any of these fonts, but not genuinely two: elsewhere in this app a
  // single line of GOTHIC_24_BOLD already needs a 34px row on its own, and
  // 28pt/18pt aren't any shorter per line. Only the SMALLEST font in the
  // ladder can plausibly fit two real lines in a box this size, so wrap
  // mode only ever tries it -- looping through the larger fonts first (as
  // this used to) let a font whose *reported* two-line content size
  // technically passed the fit check still visibly overflow the field once
  // actually drawn, since nothing here fits two lines of anything bigger.
  if (natural == 2 * N) {
    int i = N - 1;
    GFont f = fonts_get_system_font(LADDER[i].key);
    int line_h = graphics_text_layout_get_content_size(
                   "Ag", f, wide, GTextOverflowModeFill, GTextAlignmentLeft).h;
    GSize sz = graphics_text_layout_get_content_size(
                 measure_text, f, wrap, GTextOverflowModeWordWrap, GTextAlignmentLeft);
    if (sz.h <= line_h * 2 + 1 && sz.h <= box.size.h) { natural = N + i; }
  }

  int level = natural;
  if (wrap_floor) {
    if (level < *wrap_floor) level = *wrap_floor;
    *wrap_floor = level;
  }

  // An interior cursor (real content follows the caret, not just itself) has
  // no representable position in the front-truncation layout — fall back one
  // ladder rung to the two-line wrap, which DOES show the caret. Untouched
  // when the cursor sits at the tail (every existing caller), so this only
  // changes behavior once the cursor has actually been moved off the end.
  if (level == 2 * N && caret_at >= 0 && caret_at < (int)strlen(text) - 1) {
    level = 2 * N - 1;
  }
  if (level_out) *level_out = level;

  if (level < N) {   // single line
    GFont f = fonts_get_system_font(LADDER[level].key);
    GSize sz = graphics_text_layout_get_content_size(
                 text, f, wide, GTextOverflowModeFill, GTextAlignmentLeft);
    int vy = box.origin.y + (box.size.h - sz.h) / 2 - LADDER[level].rise;
    GRect tr = GRect(box.origin.x, vy, box.size.w, sz.h + 4);

    // Body in `body_color`, stopping before the caret so it can be painted in
    // its own color below. With no caret this is the whole string.
    char body[BUFFER_CAP + 8];
    const char *body_text = text;
    if (caret_at >= 0) {
      int c = caret_at < (int)sizeof(body) ? caret_at : (int)sizeof(body) - 1;
      memcpy(body, text, c); body[c] = '\0';
      body_text = body;
    }
    graphics_context_set_text_color(ctx, body_color);
    graphics_draw_text(ctx, body_text, f, tr,
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);

    // Underline the still-cycling glyph as a solid (opaque) bar spanning its
    // width, sitting just below the baseline.
    if (ul_start >= 0 && ul_end > ul_start) {
      int x0 = prv_prefix_w(text, ul_start, f);
      int x1 = prv_prefix_w(text, ul_end, f);
      int uy = vy + sz.h + 1;
      if (uy + 2 > box.origin.y + box.size.h) uy = box.origin.y + box.size.h - 2;
      if (x1 > x0) {
        graphics_context_set_fill_color(ctx, ul_color);
        graphics_fill_rect(ctx, GRect(box.origin.x + x0, uy, x1 - x0, 2), 0, GCornerNone);
      }
    }

    // Caret in its own (accent) color, painted at its measured x — just the
    // 1-byte glyph itself, not the rest of the string (which may be real
    // text now that the cursor can sit mid-string, not just the tail).
    if (caret_at >= 0) {
      int cx = prv_prefix_w(text, caret_at, f);
      graphics_context_set_text_color(ctx, caret_color);
      graphics_draw_text(ctx, "|", f,
                         GRect(box.origin.x + cx, vy, box.size.w, sz.h + 4),
                         GTextOverflowModeFill, GTextAlignmentLeft, NULL);
      // Real text after the caret, redrawn in body color at its own x (a
      // no-op empty draw when the caret sits at the tail, today's only
      // reachable case).
      int tx = prv_prefix_w(text, caret_at + 1, f);
      graphics_context_set_text_color(ctx, body_color);
      graphics_draw_text(ctx, text + caret_at + 1, f,
                         GRect(box.origin.x + tx, vy, box.size.w, sz.h + 4),
                         GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    }
    return;
  }

  if (level < 2 * N) {   // wrap to at most two lines
    int i = level - N;
    GFont f = fonts_get_system_font(LADDER[i].key);
    GSize sz = graphics_text_layout_get_content_size(
                 text, f, wrap, GTextOverflowModeWordWrap, GTextAlignmentLeft);
    int vy = box.origin.y + (box.size.h - sz.h) / 2 - LADDER[i].rise;
    if (vy < box.origin.y) vy = box.origin.y;
    graphics_draw_text(ctx, text, f, GRect(box.origin.x, vy, box.size.w, sz.h + 4),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    return;
  }

  // Truncate from the front at the smallest font until two lines hold it.
  GFont f = fonts_get_system_font(LADDER[N - 1].key);
  int line_h = graphics_text_layout_get_content_size(
                 "Ag", f, wide, GTextOverflowModeFill, GTextAlignmentLeft).h;
  const char *p = text;
  while (*p) {
    p++;
    while (((unsigned char)*p & 0xC0) == 0x80) p++;
    snprintf(buf, sizeof(buf), "...%s", p);
    GSize sz = graphics_text_layout_get_content_size(
                 buf, f, wrap, GTextOverflowModeWordWrap, GTextAlignmentLeft);
    if (sz.h <= line_h * 2 + 1 && sz.h <= box.size.h) break;
  }
  snprintf(buf, sizeof(buf), "...%s", p);
  GSize sz = graphics_text_layout_get_content_size(
               buf, f, wrap, GTextOverflowModeWordWrap, GTextAlignmentLeft);
  int vy = box.origin.y + (box.size.h - sz.h) / 2 - LADDER[N - 1].rise;
  if (vy < box.origin.y) vy = box.origin.y;
  graphics_draw_text(ctx, buf, f, GRect(box.origin.x, vy, box.size.w, sz.h + 4),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

// Recolor a palettized icon so its opaque pixels become `ink` while transparent
// palette entries stay clear. Lets one arrow asset render in any theme/state
// color. No-op if the asset didn't compile to a palettized format.
static void prv_tint_icon(GBitmap *bmp, GColor ink) {
  if (!bmp) return;
  GColor *pal = gbitmap_get_palette(bmp);
  if (!pal) return;
  int n;
  switch (gbitmap_get_format(bmp)) {
    case GBitmapFormat1BitPalette: n = 2;  break;
    case GBitmapFormat2BitPalette: n = 4;  break;
    case GBitmapFormat4BitPalette: n = 16; break;
    default: return;
  }
  for (int i = 0; i < n; i++) if (pal[i].a != 0) pal[i] = ink;
}

// Draw a ⎵ space symbol (a horizontal bar with short upturned ends), centered
// in `face` and stroked in `ink`.
static void prv_draw_space_symbol(GContext *ctx, GRect face, GColor ink) {
  int uw = face.size.w / 2, ut = 6;
  int ux = face.origin.x + (face.size.w - uw) / 2;
  int uy = face.origin.y + face.size.h * 3 / 5;
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(ux, uy), GPoint(ux + uw, uy));
  graphics_draw_line(ctx, GPoint(ux, uy), GPoint(ux, uy - ut));
  graphics_draw_line(ctx, GPoint(ux + uw, uy), GPoint(ux + uw, uy - ut));
  graphics_context_set_stroke_width(ctx, 1);
}

// Backspace (⌫) glyph for the DEL key: a left-pointing tag filled in `ink`, with
// an × knocked back out in `knockout` (the key's own fill) so the cut reads
// through to the surface. Shape adapted from the freakified pebble-calculator
// (MIT). Centered on the face, nudged 2px left so the point isn't crowded.
static GPath  *s_bksp_path = NULL;
static GPathInfo s_bksp_info = {
  .num_points = 5,
  .points = (GPoint[]){ {-10, 0}, {-3, -7}, {12, -7}, {12, 7}, {-3, 7} },
};
static void prv_draw_backspace(GContext *ctx, GRect face, GColor ink, GColor knockout) {
  GPoint c = GPoint(face.origin.x + face.size.w / 2 - 1,
                    face.origin.y + face.size.h / 2);
  if (!s_bksp_path) s_bksp_path = gpath_create(&s_bksp_info);
  gpath_move_to(s_bksp_path, c);
  graphics_context_set_fill_color(ctx, ink);
  gpath_draw_filled(ctx, s_bksp_path);
  graphics_context_set_stroke_color(ctx, knockout);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(c.x + 1, c.y - 3), GPoint(c.x + 8, c.y + 3));
  graphics_draw_line(ctx, GPoint(c.x + 1, c.y + 3), GPoint(c.x + 8, c.y - 3));
  graphics_context_set_stroke_width(ctx, 1);
}

// Navigation-page arrow icons (keys 3/5): a shaft + chevron head, stroked
// (not filled) -- the same lightweight graphics_draw_line primitive
// prv_draw_space_symbol already uses above, deliberately NOT a GPath asset
// (unlike the DEL key's own backspace tag): a filled polygon would need its
// own heap-allocated GPath plus a mutable points buffer, real bytes this
// binary is tight on (see prv_draw_backspace's own comment for that
// mechanism, still used for DEL). Mirrored by `dir` (-1 left, +1 right).
static void prv_draw_nav_arrow(GContext *ctx, GRect face, GColor ink, int dir) {
  int cx = face.origin.x + face.size.w / 2, cy = face.origin.y + face.size.h / 2;
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(cx - dir * 7, cy), GPoint(cx + dir * 2, cy));
  graphics_draw_line(ctx, GPoint(cx + dir * 2, cy), GPoint(cx - dir * 4, cy - 6));
  graphics_draw_line(ctx, GPoint(cx + dir * 2, cy), GPoint(cx - dir * 4, cy + 6));
  graphics_context_set_stroke_width(ctx, 1);
}

// One step toward black per channel — the shade a key darkens to on press.
static GColor prv_darker(GColor c) {
  c.r = c.r ? c.r - 1 : 0;
  c.g = c.g ? c.g - 1 : 0;
  c.b = c.b ? c.b - 1 : 0;
  return c;
}

// Resolve a keyboard's selected index to a theme, with the app slot at
// THEME_COUNT and any out-of-range index falling back to the first built-in.
static const Theme *prv_theme(const MultitapKeyboard *kb) {
  if (s_has_app_theme && kb->theme == THEME_COUNT) return &s_app_theme;
  int i = (kb->theme < 0 || kb->theme >= THEME_COUNT) ? 0 : kb->theme;
  return &THEMES[i];
}

static void prv_update_proc(Layer *layer, GContext *ctx) {
  MultitapKeyboard *kb = *(MultitapKeyboard **)layer_get_data(layer);
  GRect b = layer_get_bounds(layer);

  const Theme *th = prv_theme(kb);
  GColor c_bg  = (GColor){ .argb = th->bg };
  GColor c_fg  = (GColor){ .argb = th->fg };
  GColor c_key = (GColor){ .argb = th->key };
  GColor c_acc = (GColor){ .argb = th->accent };
  GColor c_act = (GColor){ .argb = th->accent_text };
  // Surface model: the screen is the soft tinted shade, while the text field and
  // the letter keys are the bright surface that floats on it. We reuse the
  // existing palette for this — `key` (the soft shade) backs the screen, `bg`
  // (the bright shade) becomes the field + letter keys — so no new theme color
  // is needed and every theme keeps its character.
  graphics_context_set_fill_color(ctx, c_bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  GFont key_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  char shown[BUFFER_CAP + 8];
  strncpy(shown, kb->buffer, sizeof(shown)); shown[sizeof(shown) - 1] = '\0';
  // Byte span of the still-cycling pending glyph within `shown`, underlined below
  // to flag it as uncommitted (the classic multi-tap cue). -1 = nothing pending.
  // Spliced in AT cursor_pos, not appended at the tail -- the pending glyph
  // will itself be inserted there once committed (prv_commit_pending ->
  // prv_emit_glyph -> prv_append, all cursor_pos-relative), so its preview
  // must show up in the same place; appending it at the tail instead (as
  // this used to, back when the cursor could only ever sit at the tail)
  // made a freshly-typed letter visibly appear at the end of the text and
  // only jump to its real position once committed, for any cursor moved
  // off the tail via the navigation page or a field tap.
  int ul_start = -1, ul_end = -1;
  if (kb->pending_active) {
    const char *eff[MAX_EFF];
    int n = prv_eff_glyphs(kb, kb->page, kb->pending_key, eff, MAX_EFF);
    if (kb->cycle_index < n) {
      char gb[8];
      prv_render_glyph(kb, eff[kb->cycle_index], gb, sizeof(gb));
      int shown_len = (int)strlen(shown);
      int gl = (int)strlen(gb);
      int pos = kb->cursor_pos;
      if (pos < 0) pos = 0;
      if (pos > shown_len) pos = shown_len;
      if (shown_len + gl < (int)sizeof(shown)) {
        memmove(shown + pos + gl, shown + pos, (size_t)(shown_len - pos + 1));
        memcpy(shown + pos, gb, (size_t)gl);
        ul_start = pos;
        ul_end = pos + gl;
      }
    }
  }
  // Splice the caret glyph in right after the pending glyph if one was just
  // spliced in above (same placement this always had), otherwise AT
  // cursor_pos directly -- the navigation page/a text-field tap can move it
  // mid-string.
  int caret_at = -1;
  if (kb->caret_on) {
    int shown_len = (int)strlen(shown);
    int pos = (ul_end >= 0) ? ul_end : kb->cursor_pos;
    if (pos < 0) pos = 0;
    if (pos > shown_len) pos = shown_len;
    if (shown_len + 1 < (int)sizeof(shown)) {
      memmove(shown + pos + 1, shown + pos, (size_t)(shown_len - pos + 1));
      shown[pos] = '|';
      caret_at = pos;
    }
  }

  // The text header is a white section (standing in for the old divider),
  // holding a plain field outlined with a 3px frame (matching the duration
  // dial boxes) where the text is drawn.
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, b.size.w, TEXT_AREA_H), 0, GCornerNone);

  GRect field = GRect(4, 4, b.size.w - 8, TEXT_AREA_H - 8);
  graphics_context_set_stroke_color(ctx, c_fg);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_rect(ctx, field);

  // Relax the sticky layout only when the committed text actually shrank (a
  // delete). Track kb->buffer, not `shown`: the blinking caret toggles shown's
  // length every frame, and a multitap holds buffer length constant while the
  // pending glyph cycles — exactly when we want the layout to stay put.
  int cur_len = (int)strlen(kb->buffer);
  if (cur_len < kb->last_text_len) kb->wrap_floor = 0;
  kb->last_text_len = cur_len;

  graphics_context_set_text_color(ctx, c_fg);
  // Pattern mode used to render alternating bold on-beats/regular breaks
  // here via its own prv_draw_pattern_text(), since that distinction was
  // once required; now that it isn't, pattern mode's text field just goes
  // through the same shared shrink-to-fit routine every other mode uses.
  prv_draw_text_fitted(ctx, shown, GRect(field.origin.x + 6, field.origin.y,
                                         field.size.w - 12, field.size.h),
                       &kb->wrap_floor, ul_start, ul_end, c_acc, caret_at, c_acc,
                       c_fg, &kb->last_display_level);

  // Keys are filled, rounded, and gapped like the throw grid: letter keys rest
  // on the soft `key` fill; the bottom function row (Shift/space/DEL) is a bold
  // accent strip — the keyboard's MISS bar. A press deepens a key for feedback:
  // each key darkens its own resting fill (keeping its label color) so a tap
  // reads as "pushed in" without changing hue. Hit testing still uses the full
  // cell (prv_key_at_point), so the gaps stay tappable.
  // Cell size + centered origin (shared with prv_key_at_point so taps land on the
  // drawn cells). GRID_VPAD above and below gives the keyboard equal white margins.
  int cw, ch; GPoint go;
  prv_grid_metrics(b, &cw, &ch, &go);
  // The active multitap key keeps the in-effect case while it cycles; every other
  // key (incl. Shift) shows the case the next press will use — so a one-shot/auto
  // capital visibly "spends" on the pressed key and the rest drop to lowercase.
  int eff_shift = prv_effective_shift(kb);
  int next_shift = prv_next_shift(kb);
  bool on_nav = prv_on_nav(kb);
  for (int i = 0; i < KEY_COUNT; i++) {
    int col = i % GRID_COLS, row = i / GRID_COLS;
    GRect face = GRect(go.x + col * cw + 2, go.y + row * ch + 3, cw - 4, ch - 4);

    bool is_active = (kb->pending_active && kb->pending_key == i);
    int  key_shift = is_active ? eff_shift : next_shift;
    bool is_function = (i == KEY_SHIFT || i == KEY_SPACE || i == KEY_DEL);
    bool shift_on = (i == KEY_SHIFT && kb->page == PAGE_ALPHA &&
                     key_shift != SHIFT_OFF);
    bool pressed = (i == kb->touch_key) || (i == kb->flash_key) ||
                   (kb->pending_active && kb->pending_key == i) || shift_on;

    GColor fill, txt;
    if (i == KEY_SHIFT || i == KEY_DEL) {
      fill = GColorDarkGray;
      txt  = GColorWhite;
    } else if (i == KEY_SPACE) {
      fill = GColorLightGray;
      txt  = GColorBlack;
    } else if (is_function) {
      fill = c_acc;    // accent strip
      txt  = c_act;
    } else {
      fill = c_key;    // soft letter
      txt  = c_fg;
    }
    GColor sink = prv_darker(fill);
    GColor draw_fill = pressed ? sink : fill;
    graphics_context_set_fill_color(ctx, draw_fill);
    graphics_fill_rect(ctx, face, 4, GCornersAll);
    graphics_context_set_text_color(ctx, txt);

    if (i == KEY_SHIFT && kb->page == PAGE_ALPHA) {
      // Shift state as an arrow icon: off (plain), once (dash), lock (dash
      // solid), recolored to the key's current text color.
      GBitmap *icon = kb->icon_shift[key_shift];
      if (icon) {
        prv_tint_icon(icon, txt);
        GSize is = gbitmap_get_bounds(icon).size;
        // +2px: the glyph sits high in its bounds, so a geometric center reads
        // a few pixels too high — nudge down to optically center.
        GRect ir = { .origin = prv_center_origin(face, is), .size = is };
        ir.origin.y += 2;
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, icon, ir);
      }
    } else if (i == KEY_SPACE && (kb->page != PAGE_NUMBERS || kb->pattern_mode)) {
      prv_draw_space_symbol(ctx, face, txt);
    } else if (i == KEY_DEL) {
      // Backspace glyph in place of a "DEL" label.
      prv_draw_backspace(ctx, face, txt, draw_fill);
    } else if (on_nav && (i == 3 || i == 5)) {
      prv_draw_nav_arrow(ctx, face, txt, i == 3 ? -1 : +1);
    } else {
      char lbl[LABEL_CAP]; prv_key_label(kb, i, key_shift, lbl, sizeof(lbl));
      // GOTHIC_18 pads above the caps, so center on its cap box (22) and nudge
      // up 2px to optically center — same correction the menu UI uses.
      GRect tr = GRect(face.origin.x + 2, face.origin.y + (face.size.h - 22) / 2, face.size.w - 2, 22);
      graphics_draw_text(ctx, lbl, key_font, tr,
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }
  }
}

// ---- Lifecycle --------------------------------------------------------------

MultitapKeyboard *multitap_keyboard_create(GRect frame, MultitapKeyboardDoneHandler done_handler,
                               void *context) {
  MultitapKeyboard *kb = malloc(sizeof(MultitapKeyboard));
  if (!kb) return NULL;
  memset(kb, 0, sizeof(MultitapKeyboard));
  kb->done_handler = done_handler;
  kb->context = context;
  kb->page = PAGE_ALPHA;
  kb->shift = SHIFT_OFF;
  kb->settings.commit_timeout_ms = MULTITAP_DEFAULT_WAIT_MS;
  kb->settings.auto_caps = true;
  for (int i = 0; i < EXT_COUNT; i++) kb->ext_enabled[i] = true;
  kb->theme = 0;
  kb->flash_key = -1;
  kb->touch_key = -1;
  kb->caret_on = true;
  kb->buffer[0] = '\0';
  kb->cursor_pos = 0;
  prv_recompute_autocap(kb);

  kb->layer = layer_create_with_data(frame, sizeof(MultitapKeyboard *));
  if (!kb->layer) { free(kb); return NULL; }
  *(MultitapKeyboard **)layer_get_data(kb->layer) = kb;
  layer_set_update_proc(kb->layer, prv_update_proc);
  kb->icon_shift[SHIFT_OFF]  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SHIFT_OFF);
  kb->icon_shift[SHIFT_ONCE] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SHIFT_ONCE);
  kb->icon_shift[SHIFT_LOCK] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SHIFT_LOCK);
  kb->caret_timer = app_timer_register(CARET_BLINK_MS, prv_caret_timer_cb, kb);
  return kb;
}

void multitap_keyboard_destroy(MultitapKeyboard *kb) {
  if (!kb) return;
  if (kb->commit_timer) { app_timer_cancel(kb->commit_timer); kb->commit_timer = NULL; }
  if (kb->caret_timer)  { app_timer_cancel(kb->caret_timer);  kb->caret_timer = NULL; }
  if (kb->flash_timer)  { app_timer_cancel(kb->flash_timer);  kb->flash_timer = NULL; }
  for (int i = 0; i < 3; i++) if (kb->icon_shift[i]) gbitmap_destroy(kb->icon_shift[i]);
  if (kb->layer) layer_destroy(kb->layer);
  if (s_bksp_path) { gpath_destroy(s_bksp_path); s_bksp_path = NULL; }   // lazily created per session
  free(kb);
}

Layer *multitap_keyboard_get_layer(MultitapKeyboard *kb) { return kb ? kb->layer : NULL; }
const char *multitap_keyboard_get_text(MultitapKeyboard *kb) { return kb ? kb->buffer : ""; }

// Trim the buffer to at most `max` bytes without splitting a UTF-8 glyph.
static void prv_clamp_to_max(MultitapKeyboard *kb) {
  if (kb->max_len <= 0) return;
  size_t i = strlen(kb->buffer);
  if (i <= (size_t)kb->max_len) {
    if (kb->cursor_pos > (int)i) kb->cursor_pos = (int)i;
    return;
  }
  i = (size_t)kb->max_len;
  while (i > 0 && ((unsigned char)kb->buffer[i] & 0xC0) == 0x80) i--;  // drop continuation
  kb->buffer[i] = '\0';
  if (kb->cursor_pos > (int)i) kb->cursor_pos = (int)i;
}

void multitap_keyboard_set_text(MultitapKeyboard *kb, const char *text) {
  if (!kb) return;
  if (kb->commit_timer) { app_timer_cancel(kb->commit_timer); kb->commit_timer = NULL; }
  kb->pending_active = false;
  if (text) { strncpy(kb->buffer, text, BUFFER_CAP - 1); kb->buffer[BUFFER_CAP - 1] = '\0'; }
  else kb->buffer[0] = '\0';
  kb->cursor_pos = (int)strlen(kb->buffer);   // resume editing at the tail, matching every caller's expectation
  prv_clamp_to_max(kb);
  prv_recompute_autocap(kb);
  layer_mark_dirty(kb->layer);
}

void multitap_keyboard_set_max_len(MultitapKeyboard *kb, int max_bytes) {
  if (!kb) return;
  kb->max_len = (max_bytes > 0 && max_bytes < BUFFER_CAP) ? max_bytes : 0;
  prv_clamp_to_max(kb);
  layer_mark_dirty(kb->layer);
}

void multitap_keyboard_set_numeric_mode(MultitapKeyboard *kb, bool enabled) {
  if (!kb) return;
  kb->numeric_mode = enabled;
  kb->locked_sub_page = 0;   // always start on the digits sub-page, never left over from a prior session
  if (enabled) kb->page = PAGE_NUMBERS;
  layer_mark_dirty(kb->layer);
}

void multitap_keyboard_set_pattern_mode(MultitapKeyboard *kb, bool enabled, bool notes_enabled) {
  if (!kb) return;
  kb->pattern_mode = enabled;
  kb->pattern_notes_enabled = notes_enabled;
  kb->locked_sub_page = 0;   // always start on the primary sub-page, never left over from a prior session
  if (enabled) kb->page = PAGE_NUMBERS;
  layer_mark_dirty(kb->layer);
}

void multitap_keyboard_get_settings(MultitapKeyboard *kb, MultitapSettings *out) {
  if (kb && out) *out = kb->settings;
}

void multitap_keyboard_set_settings(MultitapKeyboard *kb, const MultitapSettings *s) {
  if (!kb || !s) return;
  kb->settings = *s;
  if (kb->settings.commit_timeout_ms < 150) kb->settings.commit_timeout_ms = 150;
  prv_recompute_autocap(kb);
  layer_mark_dirty(kb->layer);
}

// ---- Button-shortcut actions ------------------------------------------------
// Driven by the physical watch buttons, as opposed to on-screen key taps.

void multitap_keyboard_backspace(MultitapKeyboard *kb) { if (kb) prv_press_key(kb, KEY_DEL); }

void multitap_keyboard_submit(MultitapKeyboard *kb) {
  if (!kb) return;
  prv_commit_pending(kb);
  layer_mark_dirty(kb->layer);
  // Fire last: the handler may pop/destroy the keyboard, so nothing may touch
  // `kb` after this. `buffer` stays valid for the duration of the callback.
  if (kb->done_handler) kb->done_handler(kb->buffer, kb->context);
}

void multitap_keyboard_cycle_page(MultitapKeyboard *kb) { if (kb) prv_cycle_page(kb); }

void multitap_keyboard_newline(MultitapKeyboard *kb) {
  if (!kb) return;
  prv_caret_wake(kb);
  prv_commit_pending(kb);
  prv_append(kb, "\n");
  prv_recompute_autocap(kb);
  layer_mark_dirty(kb->layer);
}

int multitap_keyboard_delete_repeat_ms(MultitapKeyboard *kb) {
  return 100;
}

// ---- Extended (toggleable) characters ---------------------------------------

int multitap_keyboard_ext_count(void) { return EXT_COUNT; }

const char *multitap_keyboard_ext_glyph(int index) {
  return (index >= 0 && index < EXT_COUNT) ? EXT_DEFS[index].glyph : "";
}

bool multitap_keyboard_ext_enabled(MultitapKeyboard *kb, int index) {
  return (kb && index >= 0 && index < EXT_COUNT) ? kb->ext_enabled[index] : false;
}

void multitap_keyboard_ext_set_enabled(MultitapKeyboard *kb, int index, bool enabled) {
  if (!kb || index < 0 || index >= EXT_COUNT) return;
  kb->ext_enabled[index] = enabled;
  kb->pending_active = false;   // cycle indices may have shifted
  layer_mark_dirty(kb->layer);
}

// ---- Themes -----------------------------------------------------------------

int multitap_keyboard_theme_count(void) { return THEME_COUNT; }   // built-ins only

const char *multitap_keyboard_theme_name(int index) {
  if (s_has_app_theme && index == THEME_COUNT) return s_app_theme.name;
  return (index >= 0 && index < THEME_COUNT) ? THEMES[index].name : "";
}

int multitap_keyboard_get_theme(MultitapKeyboard *kb) { return kb ? kb->theme : 0; }

void multitap_keyboard_set_theme(MultitapKeyboard *kb, int index) {
  int max = s_has_app_theme ? THEME_COUNT : THEME_COUNT - 1;   // app slot is last
  if (!kb || index < 0 || index > max) return;
  kb->theme = index;
  layer_mark_dirty(kb->layer);
}

UiTheme multitap_keyboard_get_theme_colors(MultitapKeyboard *kb) {
  const Theme *th = kb ? prv_theme(kb) : &THEMES[0];
  return (UiTheme){
    .background    = (GColor){ .argb = th->bg },
    .text          = (GColor){ .argb = th->fg },
    .text_muted    = (GColor){ .argb = th->fg },   // keyboard palette has no muted role
    .neutral       = (GColor){ .argb = th->key },
    .accent        = (GColor){ .argb = th->accent },
    .accent_text   = (GColor){ .argb = th->accent_text },
    .danger        = (GColor){ .argb = th->danger },
    .danger_light  = (GColor){ .argb = th->danger_light },
    .danger_darker = (GColor){ .argb = th->danger_darker },
  };
}

void multitap_keyboard_set_app_theme(const char *name, UiTheme t) {
  if (!name) { s_has_app_theme = false; return; }
  s_app_theme.name        = name;
  s_app_theme.bg          = t.background.argb;
  s_app_theme.fg          = t.text.argb;
  s_app_theme.key         = t.neutral.argb;        // ui has no "key" role → neutral
  s_app_theme.accent      = t.accent.argb;
  s_app_theme.accent_text = t.accent_text.argb;
  // The DEL key always uses this fixed danger ramp (not the caller's t.danger*
  // fields) — the same triple THEMES[] uses for every built-in, so the
  // destructive key reads the same no matter which skin is active.
  s_app_theme.danger        = GColorDarkCandyAppleRedARGB8;  // key fill
  s_app_theme.danger_light  = GColorMelonARGB8;              // glyph ink
  s_app_theme.danger_darker = GColorBulgarianRoseARGB8;      // shadow / press
  s_has_app_theme = true;
}

bool multitap_keyboard_has_app_theme(void) { return s_has_app_theme; }
int  multitap_keyboard_app_theme_index(void) { return s_has_app_theme ? THEME_COUNT : -1; }
