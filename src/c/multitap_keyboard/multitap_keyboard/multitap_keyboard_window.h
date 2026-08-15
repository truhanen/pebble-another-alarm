#pragma once
#include <pebble.h>

// =============================================================================
// multitap_keyboard_window — a full-screen modal text-entry UI built on the
// multitap_keyboard widget. This is the easy "drop-in" entry point: one call pushes
// a keyboard that owns the whole screen, wires up touch and the physical
// buttons, applies saved settings, and hands the typed text back to your app.
//
// Typical use, from anywhere in your app:
//
//     static void on_text(const char *text, void *ctx) {
//       if (text) {                 // OK pressed
//         // copy `text` (only valid during this call) and use it
//       } else {                    // Back pressed (cancelled)
//       }
//     }
//     ...
//     multitap_keyboard_window_push(on_text, "optional starting text", NULL);
//
// Buttons inside the keyboard: OK (Select) confirms, Back cancels, Up cycles
// the layout (hold Up for Settings), hold OK for a newline, Down backspaces.
//
// Single instance: only one keyboard window is shown at a time. Depends on
// multitap_keyboard.{c,h} and settings_window.{c,h}.
// =============================================================================

// Result callback. On OK, `text` is the entered string (valid ONLY during this
// call — copy it if you need to keep it). On Back/cancel, `text` is NULL. The
// keyboard window removes itself right after this returns, so the handler may
// freely push a new window (it will stay) or navigate.
typedef void (*MultitapKeyboardResultHandler)(const char *text, void *context);

// Fired after every keystroke (tap, hold, or backspace) while a pattern-mode
// keyboard is open (see multitap_keyboard_window_push_pattern's own
// live_handler param) -- `text` is the full buffer typed so far, valid only
// during the call. Never fired outside pattern mode. `space_completed` is
// true only when this call represents the buffer genuinely GROWING and its
// new tail being a space -- i.e. a segment was just freshly typed and
// completed -- as opposed to merely "the buffer happens to currently end in
// a space", which is also true right when a multi-tap key's first (still
// pending, uncommitted) press hasn't changed the buffer yet, or right after
// a backspace that lands on a leftover space. Both of those must NOT be
// treated as "a space was just entered", so callers should gate any
// playback on `space_completed` specifically, not on inspecting `text`
// themselves. Lets the caller preview a pattern live as it's typed (e.g.
// play it back the instant a space completes a segment) without needing
// its own polling.
typedef void (*MultitapKeyboardLiveTextHandler)(const char *text, bool space_completed, void *context);

// Push the full-screen keyboard. `initial_text` pre-fills the field (pass NULL
// for an empty field) and is copied internally, so it need not outlive the call.
// `context` is passed back to your handler untouched.
void multitap_keyboard_window_push(MultitapKeyboardResultHandler handler,
                             const char *initial_text, void *context);

// As above, but caps the entry at `max_len` bytes (excluding the NUL) — e.g.
// to fit a fixed-size name field. Pass 0 for no limit (same as the call above).
void multitap_keyboard_window_push_ex(MultitapKeyboardResultHandler handler,
                                const char *initial_text, int max_len,
                                void *context);

// As push_ex, but restricted to the fixed digits layout with the bottom-left
// key repurposed to cycle "*"/"-"/"/"/"," — for structured numeric/symbol
// fields (e.g. cron syntax) rather than free text. See
// multitap_keyboard_set_numeric_mode() for the underlying behavior.
void multitap_keyboard_window_push_numeric(MultitapKeyboardResultHandler handler,
                                const char *initial_text, int max_len,
                                void *context);

// As push_ex, but restricted to a small per-key duration-digit(+note)
// layout with the bottom-left key typing a literal "-" (break marker) and
// the space key typing a literal space (not "0") — for note/break duration
// patterns (e.g. "1C 2D 1- 1C 2D" with notes, "2 1- 2 1- 2" without) rather
// than free text or cron syntax. `notes_enabled` selects whether character
// keys also carry a note letter (sound patterns) or duration digits only
// (vibe patterns, which have no pitch). See multitap_keyboard_set_pattern_mode()
// for the underlying behavior. `live_handler` (may be NULL) is called after
// every keystroke with the buffer typed so far -- see
// MultitapKeyboardLiveTextHandler's own doc comment.
void multitap_keyboard_window_push_pattern(MultitapKeyboardResultHandler handler,
                                const char *initial_text, int max_len,
                                void *context, bool notes_enabled,
                                MultitapKeyboardLiveTextHandler live_handler);
