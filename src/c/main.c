// SPDX-License-Identifier: GPL-3.0-only
#include <pebble.h>
#include "alarm_calc.h"
#include "alarm_store.h"
#include "multitap_keyboard/multitap_keyboard/multitap_keyboard_window.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

static int64_t now_s(void) { return (int64_t)time(NULL); }

static void now_wall(int *wday, int *hour, int *min) {
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  *wday = tm->tm_wday;
  *hour = tm->tm_hour;
  *min = tm->tm_min;
}

// Opaque "day id" fed to ac_is_due/ac_mark_fired (see alarm_calc.h) — only
// needs to be stable within one local calendar day and differ across days;
// tm_year*400+tm_yday comfortably satisfies both without needing real epoch
// day math.
static int32_t now_day_id(void) {
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  return (int32_t)tm->tm_year * 400 + tm->tm_yday;
}

// ---- global state ----
static Window *s_window;
static MenuLayer *s_menu;
static Layer *s_main_hint_layer;   // one-alarm hint, see main_hint_update_proc
static bool s_launched_by_wakeup;   // this process exists only because a
                                    // wakeup relaunched a closed app --
                                    // see alarm_finish_ring().

static Alarm s_alarms[MAX_ALARMS];
static int s_count = 0;
static int s_order[MAX_ALARMS];   // display order (by time-of-day ascending)

static int s_first_day_of_week = 1;   // 0=Sunday, 1=Monday
// Default vibration pattern (0=Double, 1=Short, 2=Long) copied into a new
// alarm's own vibe_pattern at creation -- no longer read at ring time
// (ring reads the firing alarm's own vibe_pattern instead), since the
// pattern is now a per-alarm attribute, editable independently afterward.
static int s_default_vibe_pattern = 0;
// The phone's "Enable snooze" toggle has no watch-side counterpart: when
// off, dict.ts sends DefaultSnoozeMinutes as 0 outright, reusing the
// existing "0 = snooze disabled" convention (see alarm_snooze()) instead of
// a separate flag the watch would also have to track.
static int s_default_snooze_minutes = 9;
static int s_default_snooze_max = 3;
static uint16_t s_next_local_id = 1;
static int s_audio_volume = 0;        // 0-100, 0 = sound disabled (global, like Instant Timer's audioVolume)
static bool s_default_vibration_enabled = true;   // pre-fills new alarms' own toggles
static bool s_default_sound_enabled = true;
static bool s_default_increasing_volume = false;

static uint32_t next_alarm_id(void) {
  uint32_t id = s_next_local_id;
  s_next_local_id++;
  store_save_next_local_id(s_next_local_id);
  return id;
}

static void persist_all(void) {
  store_save(s_alarms, s_count);
  store_save_first_day_of_week(s_first_day_of_week);
  store_save_vibe_pattern(s_default_vibe_pattern);
  store_save_default_snooze_minutes(s_default_snooze_minutes);
  store_save_default_snooze_max(s_default_snooze_max);
  store_save_audio_volume(s_audio_volume);
  store_save_default_vibration_enabled(s_default_vibration_enabled);
  store_save_default_sound_enabled(s_default_sound_enabled);
  store_save_default_increasing_volume(s_default_increasing_volume);
}

// Display-only sort key for a cron alarm: lowest set bit in cron_hour_mask/
// cron_min_mask converted to hour*60+minute, same units as the legacy key,
// so cron and legacy alarms interleave in one list ordered by "earliest
// matching time-of-day, ignoring day-of-week" -- a deliberately simple
// internal tie-breaker for display/sort convenience only, never used for
// actual scheduling (compute_next_fire_time uses the real cron math).
static int cron_sort_key(const Alarm *a) {
  int hour = 0, minute = 0;
  for (int h = 0; h <= 23; h++) { if (a->cron_hour_mask & (1u << h)) { hour = h; break; } }
  for (int m = 0; m <= 59; m++) { if (a->cron_min_mask & (1ULL << m)) { minute = m; break; } }
  return hour * 60 + minute;
}

// display order: ascending by raw clock time (hour:minute), ties by index
// (stable).
static void rebuild_order(void) {
  int minutes_of_day[MAX_ALARMS];
  for (int i = 0; i < s_count; i++) {
    const Alarm *a = &s_alarms[i];
    s_order[i] = i;
    minutes_of_day[i] = a->is_cron ? cron_sort_key(a) : (a->hour * 60 + a->minute);
  }
  for (int i = 1; i < s_count; i++) {
    int key = s_order[i];
    int kv = minutes_of_day[key];
    int j = i - 1;
    while (j >= 0 && minutes_of_day[s_order[j]] > kv) {
      s_order[j + 1] = s_order[j];
      j--;
    }
    s_order[j + 1] = key;
  }
}

static void reload_ui(void) {
  rebuild_order();
  if (s_menu) { menu_layer_reload_data(s_menu); }
  if (s_main_hint_layer) { layer_mark_dirty(s_main_hint_layer); }
}

// ---- forward decls (defined further down) ----
static void rearm_wakeup(void);
static bool sweep_due_alarms(void);
static void trigger_alarm(int idx, int count);
static void open_edit_window(int idx);
static void start_new_alarm_flow(void);
static void resync_last_fired_for_schedule_change(Alarm *a);
static void confirm_window_push(const char *label0, const char *label1, void (*cb)(int, void *), void *ctx);
static int16_t bottom_bar_top_for_bounds(GRect bounds);
static Layer *bottom_bar_attach(Layer *root);

// ================================= main list =================================

static uint16_t ml_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  return (uint16_t)(s_count + 1);   // +1 for the trailing "+ New alarm" row
}

#define ML_ROW_H 58   // taller now that line 2 matches timer's detail-row font size (24pt, not 18pt)
// The trailing "+ New alarm" row has no second line, so it only needs one
// row's worth of height, not the two-line alarm rows' ML_ROW_H — same
// single-row height as the cron/repeat/confirm/edit itemized menus.
#define ML_NEW_ROW_H 34

static int16_t ml_cell_height(MenuLayer *ml, MenuIndex *cell_index, void *ctx) {
  return (cell_index->row == s_count) ? ML_NEW_ROW_H : ML_ROW_H;
}

// Row tint by state, same "state tints the row, selected row gets a
// darkened/black variant" technique as pebble-another-timer's ml_row_colors
// (main.c:2073) — there it's RUNNING/PAUSED/overtime; here it's
// snoozing/enabled/skip-pending/disabled (red/green/yellow/white), but the
// pattern (bg/fg pair per state x selection) is the same. skip-pending
// reuses timer's exact PAUSED colors (GColorYellow unselected, GColorArmyGreen
// selected) — same "a normally-active thing is temporarily not going to act"
// meaning as a paused timer. Snoozing takes priority over both, since only
// an enabled alarm can be snoozing or skip-pending.
static void ml_row_colors(bool enabled, bool snoozing, bool skip_pending, bool selected, GColor *bg, GColor *fg) {
  if (selected) {
    *fg = GColorWhite;
    if (snoozing) { *bg = PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorBlack); }
    else if (skip_pending) { *bg = PBL_IF_COLOR_ELSE(GColorArmyGreen, GColorBlack); }
    else if (enabled) { *bg = PBL_IF_COLOR_ELSE(GColorDarkGreen, GColorBlack); }
    else { *bg = GColorBlack; }
  } else if (snoozing) {
    *fg = GColorBlack;
    *bg = PBL_IF_COLOR_ELSE(GColorRed, GColorWhite);
  } else if (skip_pending) {
    *fg = GColorBlack;
    *bg = PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite);
  } else if (enabled) {
    *fg = GColorBlack;
    *bg = PBL_IF_COLOR_ELSE(GColorMediumSpringGreen, GColorWhite);
  } else {
    *fg = GColorDarkGray;
    *bg = GColorWhite;
  }
}

static bool ml_is_boundary_row(int row) {
  return row < s_count;   // every alarm row has a divider below it; the
                          // trailing "+ New alarm" row is the list's end
}

// Leading play/stop/skip indicator for an alarm row's first line, ported
// from pebble-another-timer's ml_draw_state_icon (its TS_RUNNING/TS_STOPPED
// cases only -- this app adds a third, ICON_SKIP, that timer has no
// equivalent for). "Play": a scanline-built right-pointing triangle (flat
// vertical left edge at x, tapering to a point at mid-height) -- widest span
// in the middle row, narrowing by row-distance from center, same technique
// timer uses for its running icon. "Stop": a plain filled square. "Skip":
// the same square, in the same `fg` tint as play/stop (not a special color
// of its own), with a "1" digit cut into it in the row's `bg` tint for
// contrast -- one specific occurrence is being skipped, not the alarm being
// stopped outright.
typedef enum { ML_ICON_PLAY, ML_ICON_STOP, ML_ICON_SKIP } MlIcon;
static void ml_draw_state_icon(GContext *ctx, int x, int y, MlIcon icon, GColor fg, GColor bg) {
  if (icon == ML_ICON_SKIP) {
    graphics_context_set_fill_color(ctx, fg);
    graphics_fill_rect(ctx, GRect(x, y + 1, 11, 11), 0, GCornerNone);
    graphics_context_set_text_color(ctx, bg);
    graphics_draw_text(ctx, "1", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(x - 1, y - 3, 14, 16), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    return;
  }
  graphics_context_set_fill_color(ctx, fg);
  if (icon == ML_ICON_STOP) {
    graphics_fill_rect(ctx, GRect(x, y + 1, 11, 11), 0, GCornerNone);
    return;
  }
  const int h = 12;
  const int w = 10;
  for (int row = 0; row < h; row++) {
    int d = (row <= (h / 2)) ? ((h / 2) - row) : (row - (h / 2));
    int span = w - (d * w) / (h / 2 + 1);
    if (span < 1) { span = 1; }
    graphics_fill_rect(ctx, GRect(x, y + row, span, 1), 0, GCornerNone);
  }
}

static void ml_draw_new_alarm_row(GContext *ctx, const Layer *cell_layer, bool selected) {
  GRect b = layer_get_bounds(cell_layer);
  graphics_context_set_fill_color(ctx, selected ? GColorBlack : GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, selected ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, "+ New alarm", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(4, (b.size.h - 26) / 2, b.size.w - 8, 26),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void ml_draw_alarm_row(GContext *ctx, const Layer *cell_layer, int idx, int row, bool selected) {
  const Alarm *a = &s_alarms[idx];
  GRect b = layer_get_bounds(cell_layer);

  bool skip_pending = a->enabled && (a->repeats || a->is_cron) && a->skip_next;
  GColor bg, fg;
  ml_row_colors(a->enabled, a->snooze_until > 0, skip_pending, selected, &bg, &fg);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, fg);

  // Line 1: fixed-width bold time (column-aligned, like timer's HH:MM:SS),
  // then the label in a lighter weight — same visual
  // hierarchy as timer's ml_draw_row single-line layout (main.c:2205-2231).
  bool small = (b.size.w <= 144);
  GFont tf = fonts_get_system_font(small ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_24_BOLD);
  GFont nf = fonts_get_system_font(small ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_24);
  GFont lf = fonts_get_system_font(small ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_24);   // line 2, same size step as timer's detail row
  int th = small ? 22 : 28;
  int line2_h = th;
  // Center the whole 2-line block vertically in the cell, including timer's
  // -2 nudge — same approach as timer's single-line `ty = (b.size.h - th) / 2
  // - 2` (main.c:2212), just applied to our (line1 + line2) content height
  // instead of a single line.
  int ty = (b.size.h - (th + line2_h)) / 2 - 2;
  int time_x = 6;

  // Leading play/stop/skip icon: enabled -> play; enabled but skipping its
  // next occurrence (mirrors edit_draw_row's EDIT_ROW_ENABLE "Skip next"
  // condition exactly) -> skip; otherwise (disabled) -> stop.
  int icon_y = ty + (th - 12) / 2 + 3;
  MlIcon icon = !a->enabled ? ML_ICON_STOP : skip_pending ? ML_ICON_SKIP : ML_ICON_PLAY;
  ml_draw_state_icon(ctx, time_x, icon_y, icon, fg, bg);
  // The skip icon's "1" digit sets the context's text color to bg; restore
  // it to fg before drawing the rest of the row's text.
  graphics_context_set_text_color(ctx, fg);
  int time_text_x = time_x + 16;

  char time_buf[16];
  if (a->is_cron) { snprintf(time_buf, sizeof(time_buf), "Cron"); }
  else { ac_format_time(time_buf, sizeof(time_buf), a->hour, a->minute, clock_is_24h_style()); }
  graphics_draw_text(ctx, time_buf, tf, GRect(time_text_x, ty, b.size.w - time_text_x - 4, th),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  GSize tw = graphics_text_layout_get_content_size(time_buf, tf,
    GRect(0, 0, b.size.w, th), GTextOverflowModeFill, GTextAlignmentLeft);
  int desc_x = time_text_x + tw.w + 8;
  if (a->name[0]) {
    graphics_draw_text(ctx, a->name, nf, GRect(desc_x, ty, b.size.w - 4 - desc_x, th),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }

  // Line 2: repeat summary (or the raw cron field triple for cron alarms).
  char repeat_buf[48];
  if (a->is_cron) { ac_format_cron_summary(repeat_buf, sizeof(repeat_buf), a->cron_min, a->cron_hour, a->cron_dow); }
  else { ac_format_repeat_summary(repeat_buf, sizeof(repeat_buf), a->repeats, a->repeat_days, s_first_day_of_week); }
  graphics_draw_text(ctx, repeat_buf, lf,
                     GRect(time_x, ty + th, b.size.w - time_x - 4, line2_h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  if (ml_is_boundary_row(row)) {
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_line(ctx, GPoint(0, b.size.h - 1), GPoint(b.size.w - 1, b.size.h - 1));
  }
}

static void ml_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *ctx2) {
  int row = cell_index->row;
  MenuIndex sel = menu_layer_get_selected_index(s_menu);
  if (row == s_count) { ml_draw_new_alarm_row(ctx, cell_layer, sel.row == row); return; }
  ml_draw_alarm_row(ctx, cell_layer, s_order[row], row, sel.row == row);
}

// Short-press SELECT on an alarm row cycles its state directly, with no
// intervening confirm prompt — same underlying fields as the edit menu's
// State row (edit_select's EDIT_ROW_ENABLE case), just reachable in one
// press from the main list instead of opening the edit menu and confirming
// a choice there. For a repeating OR cron alarm the cycle is three states,
// always advancing the same direction regardless of how it got there:
// enabled (normal) -> disabled -> enabled-but-skip-next-occurrence -> back
// to enabled (normal). A plain non-repeating, non-cron alarm has no skip
// state (skip_next is meaningless for a one-time alarm — it just
// auto-disables once it fires), so it's a plain two-state enabled/disabled
// toggle.
static void ml_cycle_alarm_state(int idx) {
  if (idx < 0 || idx >= s_count) { return; }
  Alarm *a = &s_alarms[idx];
  if (!a->repeats && !a->is_cron) {
    bool was_enabled = a->enabled;
    a->enabled = !a->enabled;
    a->skip_next = false;
    if (!was_enabled && a->enabled) { resync_last_fired_for_schedule_change(a); }
  } else if (a->enabled && !a->skip_next) {
    a->enabled = false;
  } else if (!a->enabled) {
    a->enabled = true;
    a->skip_next = true;
    // resync_last_fired_for_schedule_change reads repeat_days under LEGACY
    // (weekly-repeat weekday) semantics -- a cron alarm repurposes
    // repeat_days as its day-of-week mask, so it must take the "no
    // eager-fire guard needed" cron path instead, same split as
    // edit_select's EDIT_ROW_ENABLE re-enable handler.
    if (a->is_cron) { a->cron_last_fired_min = -1; }
    else { resync_last_fired_for_schedule_change(a); }
  } else {
    a->skip_next = false;
  }
  persist_all(); rearm_wakeup(); reload_ui();
}

static void ml_select(MenuLayer *ml, MenuIndex *cell_index, void *ctx) {
  int row = cell_index->row;
  if (row == s_count) { start_new_alarm_flow(); return; }
  ml_cycle_alarm_state(s_order[row]);
}

// Long-press SELECT opens the full edit menu — the main list's short-press
// now owns the enable/disable toggle, so editing every other field moves
// behind a long-press instead of the previous single short-press.
static void ml_select_long(MenuLayer *ml, MenuIndex *cell_index, void *ctx) {
  int row = cell_index->row;
  if (row == s_count) { return; }   // no long-press action on "+ New alarm"
  open_edit_window(s_order[row]);
}

// ================================= scheduling =================================

// Converts a (day_offset, hour, minute) triple into a real time_t via
// localtime/mktime, which normalizes month/year rollover and DST. Shared by
// both legacy alarms (fixed hour/minute) and cron alarms (a computed match),
// which is why it takes hour/minute directly rather than an Alarm*.
static time_t occurrence_to_epoch_hm(int day_offset, int hour, int minute) {
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  tm.tm_mday += day_offset;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = 0;
  return mktime(&tm);
}

static time_t occurrence_to_epoch(const Alarm *a, int day_offset) {
  return occurrence_to_epoch_hm(day_offset, a->hour, a->minute);
}

// Picks the soonest instant across (a) any currently-snoozed alarm's deadline
// and (b) every enabled, non-snoozed alarm's next regular occurrence.
// Returns true + *out_time/*out_idx set, or false if nothing is scheduled.
static bool compute_next_fire_time(int64_t *out_time, int *out_idx) {
  bool found = false;
  int64_t best = 0;
  int best_idx = -1;

  for (int i = 0; i < s_count; i++) {
    if (s_alarms[i].snooze_until > 0) {
      if (!found || s_alarms[i].snooze_until < best) { best = s_alarms[i].snooze_until; best_idx = i; found = true; }
    }
  }

  int wday, hour, min;
  now_wall(&wday, &hour, &min);
  // Alarms with an active snooze deadline are excluded from the regular
  // occurrence scan below (their schedule is paused until the snooze fires).
  // Cron alarms are also excluded here (ac_next_occurrence reads a->hour/
  // a->minute, meaningless for is_cron) and scanned separately below instead.
  Alarm scan[MAX_ALARMS];
  int scan_map[MAX_ALARMS];
  int scan_count = 0;
  for (int i = 0; i < s_count; i++) {
    if (s_alarms[i].snooze_until > 0 || s_alarms[i].is_cron) { continue; }
    scan[scan_count] = s_alarms[i];
    scan_map[scan_count] = i;
    scan_count++;
  }
  int idx_in_scan = -1;
  int minutes = ac_next_occurrence(scan, scan_count, wday, hour, min, &idx_in_scan);
  if (minutes >= 0) {
    // NOTE: day_offset must be recomputed via ac_next_offset_days, not derived
    // from `minutes / 1440` — ac_next_occurrence's "minutes from now" is
    // off*1440 + a_total - now_total, and once the alarm's time-of-day has
    // already passed today (the common case for any off >= 1), a_total -
    // now_total is negative, which makes integer division truncate the
    // recovered offset to one less than the real value. That silently built
    // an already-past epoch, which made rearm_wakeup fall back to "now + 1s"
    // and re-arm an immediate wakeup forever — the app relaunching to the
    // main screen in a tight loop with no alarm ever due.
    const Alarm *winner = &s_alarms[scan_map[idx_in_scan]];
    int day_offset = ac_next_offset_days(winner, wday, hour, min);
    time_t candidate = occurrence_to_epoch(winner, day_offset);
    if (!found || (int64_t)candidate < best) { best = (int64_t)candidate; best_idx = scan_map[idx_in_scan]; found = true; }
  }

  for (int i = 0; i < s_count; i++) {
    const Alarm *a = &s_alarms[i];
    if (!a->is_cron || !a->enabled || a->snooze_until > 0) { continue; }
    int oh, om;
    int off = ac_cron_next_offset_days(a->cron_min_mask, a->cron_hour_mask, a->repeat_days, a->skip_next,
                                        wday, hour, min, &oh, &om);
    if (off < 0) { continue; }
    time_t candidate = occurrence_to_epoch_hm(off, oh, om);
    if (!found || (int64_t)candidate < best) { best = (int64_t)candidate; best_idx = i; found = true; }
  }

  if (!found) { return false; }
  if (out_time) { *out_time = best; }
  if (out_idx) { *out_idx = best_idx; }
  return true;
}

// Keep exactly ONE wakeup armed for the soonest due alarm (snoozed or
// regular), same "arm new before cancelling old" ordering as
// pebble-another-timer's rearm_wakeup so a transiently-refused reschedule
// never leaves the app with no wakeup at all.
static void rearm_wakeup(void) {
  int32_t old = store_load_wakeup_id();
  int64_t fire_time;
  int idx;
  if (!compute_next_fire_time(&fire_time, &idx)) {
    if (old >= 0) { wakeup_cancel(old); store_save_wakeup_id(-1); }
    return;
  }
  time_t nowt = time(NULL);
  time_t base = (time_t)fire_time;
  if (base <= nowt) { base = nowt + 1; }

  WakeupId id = wakeup_schedule(base, 0, true);
  if (id >= 0) {
    if (old >= 0 && old != id) { wakeup_cancel(old); }
    store_save_wakeup_id(id);
    return;
  }
  if (old >= 0) { wakeup_cancel(old); store_save_wakeup_id(-1); }
  time_t when = base;
  for (int attempt = 0; attempt < 5; attempt++) {
    id = wakeup_schedule(when, 0, true);
    if (id >= 0) { store_save_wakeup_id(id); return; }
    when += 60;
  }
  APP_LOG(APP_LOG_LEVEL_WARNING, "wakeup_schedule failed after retries");
}

// For each alarm, check whether its due instant (a live snooze deadline, or
// its next regular occurrence) has passed; if so and it isn't already
// alarm_pending, mark it pending. A REGULAR (non-snoozed) transition also
// calls ac_mark_fired() and resets that alarm's snooze counter (spec §9.4:
// resets the moment the next scheduled occurrence fires) — done exactly
// once here, guarded by alarm_pending itself so it never re-fires.
static bool sweep_due_alarms(void) {
  int64_t now = now_s();
  int wday, hour, min;
  now_wall(&wday, &hour, &min);
  int32_t today = now_day_id();
  int32_t epoch_min = (int32_t)(now / 60);
  bool any = false;
  for (int i = 0; i < s_count; i++) {
    Alarm *a = &s_alarms[i];
    if (a->alarm_pending) { any = true; continue; }
    if (a->snooze_until > 0) {
      if (now >= a->snooze_until) {
        a->alarm_pending = true;
        a->snooze_until = 0;
        any = true;
      }
      continue;
    }
    if (a->is_cron) {
      if (!ac_cron_is_due(a->cron_min_mask, a->cron_hour_mask, a->repeat_days, a->enabled,
                           wday, hour, min, epoch_min, a->cron_last_fired_min)) { continue; }
      // Multi-fire, deliberately no suppression window beyond the exact-
      // minute dedup above: the very next matching minute (e.g. every
      // minute for "*") fires again right after this one is stopped/
      // snoozed — intended cron behavior, not a bug.
      a->cron_last_fired_min = epoch_min;
      a->snooze_count = 0;
      // This is the resumed (non-skipped) occurrence actually ringing --
      // rearm_wakeup()'s cron scan (compute_next_fire_time) already skipped
      // scheduling a wakeup for whatever match skip_next was covering, so by
      // the time sweep ever sees a cron alarm as due again, any pending skip
      // has already served its purpose. Clear it here, mirroring
      // ac_mark_fired()'s legacy skip_next consumption on its own resumed
      // occurrence.
      if (a->skip_next) { a->skip_next = false; }
      a->alarm_pending = true;
      any = true;
      continue;
    }
    if (!ac_is_due(a, wday, hour, min, today)) { continue; }
    // Regular occurrence just became due.
    ac_mark_fired(a, today);   // also resets a->snooze_count, stamps last_fired_day
    a->alarm_pending = true;
    any = true;
  }
  return any;
}

// Lower is a stronger signal: sound beats vibration beats neither. Used to
// pick which of several simultaneously-pending alarms gets the ring screen
// and fires its signal first, so a silent (or vibrate-only) alarm never
// pre-empts one that would otherwise wake the user with sound.
static int alarm_signal_strength(const Alarm *a) {
  if (a->sound_enabled) { return 0; }
  if (a->vibration_enabled) { return 1; }
  return 2;
}

static int first_pending_alarm_idx(void) {
  int best = -1;
  for (int i = 0; i < s_count; i++) {
    if (!s_alarms[i].alarm_pending) { continue; }
    if (best < 0 || alarm_signal_strength(&s_alarms[i]) < alarm_signal_strength(&s_alarms[best])) {
      best = i;
    }
  }
  return best;
}

static int pending_alarm_count(void) {
  int n = 0;
  for (int i = 0; i < s_count; i++) { if (s_alarms[i].alarm_pending) { n++; } }
  return n;
}

static bool show_next_pending_alarm(void) {
  int idx = first_pending_alarm_idx();
  if (idx < 0) { return false; }
  trigger_alarm(idx, pending_alarm_count());
  return true;
}

// ================================= ring screen =================================

#define ALARM_BUZZ_INTERVAL_MS 4000
#define ALARM_BUZZ_MAX_S       600
#define ALARM_AUTO_STOP_MS 2000   // "a couple seconds"
#define ALARM_VOLUME_STEP 10   // increasing_volume: volume units added per buzz cycle (after the initial 0->1->5 warm-up)

static Window *s_alarm_window;
static TextLayer *s_alarm_title;
static TextLayer *s_alarm_sub;
static TextLayer *s_alarm_lbl_up;
static TextLayer *s_alarm_lbl_down;
static int s_alarm_idx = -1;
static char s_alarm_title_buf[16 + NAME_LEN + 2];
static char s_alarm_sub_buf[32];
static AppTimer *s_alarm_buzz_timer;
static int64_t s_alarm_buzz_start_s;
static int s_alarm_buzz_count;   // buzz cycles completed since this ring started; drives increasing_volume (see alarm_current_volume)
static AppTimer *s_alarm_auto_stop_timer;   // auto_stop: auto-dismisses the ring screen

static void alarm_vibrate(uint8_t pattern) {
  switch (pattern) {
    case 1: vibes_short_pulse(); break;
    case 2: vibes_long_pulse(); break;
    default: vibes_double_pulse(); break;
  }
}

// Same note sequence, PBL_SPEAKER guard, and mute check as
// pebble-instant-timer's alarm_play_audio() (src/c/instant_timer.c, "Add
// alarm audio" commit): a beep-silence-beep-silence pattern, gated on the
// (global, phone-configured) volume being nonzero and the speaker not being
// system-muted. Ours additionally requires the firing alarm's own
// sound_enabled toggle (checked by the caller) — Instant Timer has no
// per-alarm concept to gate on.
#if PBL_SPEAKER
static void alarm_play_audio(uint8_t volume) {
  static const SpeakerNote beep = {
    .midi_note = 95, .waveform = SpeakerWaveformSquare, .duration_ms = 150, .velocity = 0, .reserved = 0
  };
  static const SpeakerNote silence = {
    .midi_note = 0, .waveform = SpeakerWaveformSine, .duration_ms = 100, .velocity = 0, .reserved = 0
  };
  static const SpeakerNote notes[4] = { beep, silence, beep, silence };
  if (volume > 0 && !speaker_is_muted()) {
    speaker_play_notes(notes, ARRAY_LENGTH(notes), volume);
  }
}

// increasing_volume: driven by s_alarm_buzz_count (how many buzz cycles have
// already happened since this ring started, reset alongside
// s_alarm_buzz_start_s at the top of trigger_alarm() regardless of
// auto_stop), NOT by elapsed time -- a fixed per-cycle step reads as a much
// more noticeably increasing ramp than a fixed total duration does, since
// most of a duration-based ramp's early steps landed on volumes too low to
// be audibly distinct from silence on the watch's speaker. Sequence: 0, 0
// (silent on the first two buzzes), 1, 1 (a bare-minimum audible cue, held
// for two buzzes as well), 5, 10, then increments of ALARM_VOLUME_STEP (10,
// so 20, 30, 40, ...) from there on, capped at the configured global
// s_audio_volume. Vibration is unaffected by this -- it always fires at
// full strength from the very first buzz cycle, so an increasing-volume
// alarm isn't inaudible-and-unnoticeable at the very start when vibration
// is also on.
static uint8_t alarm_current_volume(const Alarm *a) {
  if (!a->increasing_volume) { return (uint8_t)s_audio_volume; }
  int c = s_alarm_buzz_count;
  int vol;
  if (c <= 1) { vol = 0; }
  else if (c <= 3) { vol = 1; }
  else if (c == 4) { vol = 5; }
  else if (c == 5) { vol = 10; }
  else { vol = (c - 4) * ALARM_VOLUME_STEP; }
  if (vol > s_audio_volume) { vol = s_audio_volume; }
  return (uint8_t)vol;
}
#endif // PBL_SPEAKER

static void alarm_buzz_cb(void *ctx) {
  s_alarm_buzz_timer = NULL;
  if (now_s() - s_alarm_buzz_start_s >= ALARM_BUZZ_MAX_S) { return; }
  if (s_alarm_idx >= 0 && s_alarm_idx < s_count) {
    if (s_alarms[s_alarm_idx].vibration_enabled) { alarm_vibrate(s_alarms[s_alarm_idx].vibe_pattern); }
#if PBL_SPEAKER
    if (s_alarms[s_alarm_idx].sound_enabled) {
      alarm_play_audio(alarm_current_volume(&s_alarms[s_alarm_idx]));
    }
#endif // PBL_SPEAKER
    // Advances the increasing_volume ramp one step regardless of whether
    // sound is currently enabled/available this cycle, so the ramp position
    // stays consistent with "how many times has this alarm buzzed" rather
    // than depending on PBL_SPEAKER or the alarm's own sound_enabled toggle.
    s_alarm_buzz_count++;
  }
  s_alarm_buzz_timer = app_timer_register(ALARM_BUZZ_INTERVAL_MS, alarm_buzz_cb, NULL);
}

static void alarm_buzz_start(void) {
  if (s_alarm_buzz_timer) { app_timer_cancel(s_alarm_buzz_timer); s_alarm_buzz_timer = NULL; }
  s_alarm_buzz_start_s = now_s();
  alarm_buzz_cb(NULL);
}

static void alarm_buzz_stop(void) {
  if (s_alarm_buzz_timer) { app_timer_cancel(s_alarm_buzz_timer); s_alarm_buzz_timer = NULL; }
  vibes_cancel();
#if PBL_SPEAKER
  speaker_stop();
#endif // PBL_SPEAKER
}

// auto_stop's ring screen auto-dismisses itself via s_alarm_auto_stop_timer
// (armed in trigger_alarm); a manual Stop or Snooze pressed before it fires
// must cancel it, or it could fire moments later and clobber whatever the
// user just did (re-clearing a snooze the user just set, or re-running the
// "show next pending" chain a second time).
static void alarm_cancel_auto_stop(void) {
  if (s_alarm_auto_stop_timer) { app_timer_cancel(s_alarm_auto_stop_timer); s_alarm_auto_stop_timer = NULL; }
}

// Called once a ring screen has nothing left to chain to (see
// show_next_pending_alarm) after a Stop/Snooze/auto-stop. If this whole app
// process only exists because a wakeup relaunched it while it wasn't
// already open, exit back out entirely (window_stack_pop_all -- the same
// "closest thing to never having opened it" mechanism this app has used
// before) instead of leaving the main list on screen, since the user never
// asked to open the app for this. A wakeup firing while the app was
// already foregrounded leaves s_launched_by_wakeup false, so that case is
// unaffected and still returns to the main list.
static void alarm_finish_ring(void) {
  if (show_next_pending_alarm()) { return; }
  if (s_launched_by_wakeup) {
    window_stack_pop_all(false);
  } else {
    window_stack_remove(s_alarm_window, true);
  }
}

static void alarm_do_stop(void) {
  alarm_cancel_auto_stop();
  if (s_alarm_idx >= 0 && s_alarm_idx < s_count) {
    s_alarms[s_alarm_idx].alarm_pending = false;
    s_alarms[s_alarm_idx].snooze_until = 0;
    s_alarms[s_alarm_idx].snooze_count = 0;
    persist_all(); rearm_wakeup(); reload_ui();
  }
  alarm_finish_ring();
}
static void alarm_stop(ClickRecognizerRef rec, void *ctx) { alarm_do_stop(); }

// Fired ALARM_AUTO_STOP_MS after an auto_stop alarm's ring screen is shown
// (see trigger_alarm) -- same end state as the user pressing Stop.
static void alarm_auto_stop_cb(void *data) {
  s_alarm_auto_stop_timer = NULL;
  alarm_do_stop();
}

static void alarm_snooze(ClickRecognizerRef rec, void *ctx) {
  alarm_cancel_auto_stop();
  int idx = s_alarm_idx;
  if (idx >= 0 && idx < s_count) {
    Alarm *a = &s_alarms[idx];
    if (a->snooze_minutes == 0) {
      // Snooze disabled outright: behave like Stop instead.
      alarm_stop(rec, ctx);
      return;
    }
    if (a->snooze_max != 0 && a->snooze_count >= a->snooze_max) {
      // Out of snoozes: behave like Stop instead.
      alarm_stop(rec, ctx);
      return;
    }
    a->alarm_pending = false;
    a->snooze_until = now_s() + (int64_t)a->snooze_minutes * 60;
    a->snooze_count++;
    persist_all(); rearm_wakeup(); reload_ui();
  }
  alarm_finish_ring();
}

// Explicitly subscribed as a no-op: an UNBOUND back button pops the window by
// default, which would silently dismiss the alarm — the opposite of the
// "BACK does nothing, ringing continues" decision (spec §9.5/§7).
static void alarm_back_noop(ClickRecognizerRef rec, void *ctx) {}

static void alarm_click_config(void *ctx) {
  window_multi_click_subscribe(BUTTON_ID_DOWN, 2, 2, 400, true, alarm_stop);
  window_single_click_subscribe(BUTTON_ID_UP, alarm_snooze);
  window_single_click_subscribe(BUTTON_ID_BACK, alarm_back_noop);
}

static GFont alarm_title_font(const char *text, int box_w, int box_h, GSize *out) {
  static const char *const keys[] = {
    FONT_KEY_BITHAM_42_BOLD,
    FONT_KEY_BITHAM_30_BLACK,
    FONT_KEY_GOTHIC_28_BOLD,
    FONT_KEY_GOTHIC_24_BOLD,
    FONT_KEY_GOTHIC_18_BOLD,
  };
  const GRect probe = GRect(0, 0, box_w, 2000);
  GFont chosen = NULL;
  for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    GFont f = fonts_get_system_font(keys[i]);
    GSize sz = graphics_text_layout_get_content_size(
        text, f, probe, GTextOverflowModeWordWrap, GTextAlignmentCenter);
    chosen = f; *out = sz;
    if (sz.h <= box_h) { break; }
  }
  return chosen;
}

static void layout_alarm_title(void) {
  if (!s_alarm_title || !s_alarm_window) { return; }
  GRect b = layer_get_bounds(window_get_root_layer(s_alarm_window));
  const int h = b.size.h, wd = b.size.w;
  const int up_bottom = h * 22 / 100 - 16 + 34;
  const int down_top  = h * 78 / 100 - 18;
  const int band_top = up_bottom + 2;
  const int band_h   = down_top - 2 - band_top;
  const int box_w = wd - 4;
  GSize sz;
  GFont tf = alarm_title_font(s_alarm_title_buf, box_w, band_h, &sz);
  const int used_h = sz.h < band_h ? sz.h : band_h;
  const int title_y = band_top + (band_h - used_h) / 2;
  text_layer_set_font(s_alarm_title, tf);
  layer_set_frame(text_layer_get_layer(s_alarm_title), GRect(2, title_y, box_w, used_h + 4));
}

static void alarm_window_load(Window *w) {
  window_set_background_color(w, GColorRed);
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  const int h = b.size.h, wd = b.size.w;

  s_alarm_sub = text_layer_create(GRect(4, 2, wd - 8, 28));
  text_layer_set_background_color(s_alarm_sub, GColorClear);
  text_layer_set_text_color(s_alarm_sub, GColorWhite);
  text_layer_set_font(s_alarm_sub, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text_alignment(s_alarm_sub, GTextAlignmentCenter);
  text_layer_set_text(s_alarm_sub, s_alarm_sub_buf);
  layer_add_child(root, text_layer_get_layer(s_alarm_sub));

  s_alarm_lbl_up = text_layer_create(GRect(0, h * 22 / 100 - 31, wd - 6, 34));
  text_layer_set_background_color(s_alarm_lbl_up, GColorClear);
  text_layer_set_text_color(s_alarm_lbl_up, GColorWhite);
  text_layer_set_font(s_alarm_lbl_up, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_alarm_lbl_up, GTextAlignmentRight);
  text_layer_set_text(s_alarm_lbl_up, "Snooze");
  layer_add_child(root, text_layer_get_layer(s_alarm_lbl_up));

  s_alarm_title = text_layer_create(GRect(2, h / 2 - 36, wd - 4, 72));
  text_layer_set_background_color(s_alarm_title, GColorClear);
  text_layer_set_text_color(s_alarm_title, GColorWhite);
  text_layer_set_text_alignment(s_alarm_title, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_alarm_title, GTextOverflowModeWordWrap);
  text_layer_set_text(s_alarm_title, s_alarm_title_buf);
  layer_add_child(root, text_layer_get_layer(s_alarm_title));

  layout_alarm_title();

  s_alarm_lbl_down = text_layer_create(GRect(0, h * 78 / 100 - 6, wd - 6, 34));
  text_layer_set_background_color(s_alarm_lbl_down, GColorClear);
  text_layer_set_text_color(s_alarm_lbl_down, GColorWhite);
  text_layer_set_font(s_alarm_lbl_down, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_alarm_lbl_down, GTextAlignmentRight);
  text_layer_set_text(s_alarm_lbl_down, "Stop x2");
  layer_add_child(root, text_layer_get_layer(s_alarm_lbl_down));
}

static void alarm_window_unload(Window *w) {
  alarm_buzz_stop();
  alarm_cancel_auto_stop();
  text_layer_destroy(s_alarm_title); s_alarm_title = NULL;
  text_layer_destroy(s_alarm_sub); s_alarm_sub = NULL;
  text_layer_destroy(s_alarm_lbl_up); s_alarm_lbl_up = NULL;
  text_layer_destroy(s_alarm_lbl_down); s_alarm_lbl_down = NULL;
}

static void trigger_alarm(int idx, int count) {
  if (idx < 0 || idx >= s_count) { return; }
  const Alarm *a = &s_alarms[idx];
  char time_buf[16];
  if (a->is_cron) {
    // A cron alarm has no single fixed hour/minute to show -- its own
    // fields are unused/stale (see alarm_calc.h's is_cron doc comment), so
    // show the actual instant it's ringing at instead.
    int wday, hour, min;
    now_wall(&wday, &hour, &min);
    ac_format_time(time_buf, sizeof(time_buf), (uint8_t)hour, (uint8_t)min, clock_is_24h_style());
  } else {
    ac_format_time(time_buf, sizeof(time_buf), a->hour, a->minute, clock_is_24h_style());
  }
  if (a->name[0]) { snprintf(s_alarm_title_buf, sizeof(s_alarm_title_buf), "%s", a->name); }
  else { snprintf(s_alarm_title_buf, sizeof(s_alarm_title_buf), "%s", time_buf); }
  if (count > 1) { snprintf(s_alarm_sub_buf, sizeof(s_alarm_sub_buf), "+%d more", count - 1); }
  else { s_alarm_sub_buf[0] = '\0'; }
  s_alarm_idx = idx;

  if (!s_alarm_window) {
    s_alarm_window = window_create();
    window_set_window_handlers(s_alarm_window, (WindowHandlers){
      .load = alarm_window_load, .unload = alarm_window_unload });
    window_set_click_config_provider(s_alarm_window, alarm_click_config);
  }
  if (!window_stack_contains_window(s_alarm_window)) {
    window_stack_push(s_alarm_window, true);
  } else {
    if (s_alarm_title) { text_layer_set_text(s_alarm_title, s_alarm_title_buf); layout_alarm_title(); }
    if (s_alarm_sub) { text_layer_set_text(s_alarm_sub, s_alarm_sub_buf); }
  }

  alarm_cancel_auto_stop();   // never leave a stale timer armed against whichever alarm gets shown next
  // Stamped/reset here (not just inside alarm_buzz_start()) so
  // alarm_current_volume()/s_alarm_buzz_count always start fresh for THIS
  // ring, even for the auto_stop branch below, which never calls
  // alarm_buzz_start() at all.
  s_alarm_buzz_start_s = now_s();
  s_alarm_buzz_count = 0;
  if (a->auto_stop) {
    if (a->vibration_enabled) { alarm_vibrate(a->vibe_pattern); }
#if PBL_SPEAKER
    // Note: auto_stop only ever buzzes once, at buzz_count=0, so an
    // increasing_volume alarm combined with auto_stop never reaches a
    // meaningfully ramped-up volume -- see alarm_current_volume().
    if (a->sound_enabled) { alarm_play_audio(alarm_current_volume(a)); }
#endif // PBL_SPEAKER
    s_alarm_auto_stop_timer = app_timer_register(ALARM_AUTO_STOP_MS, alarm_auto_stop_cb, NULL);
  } else {
    alarm_buzz_start();
  }
}

// ============================ generic 2-choice confirm ============================

typedef void (*ConfirmChoiceCb)(int choice, void *ctx);   // choice: 0 or 1

static Window *s_confirm_window;
static MenuLayer *s_confirm_menu;
static const char *s_confirm_labels[2];
static ConfirmChoiceCb s_confirm_cb;
static void *s_confirm_ctx;

static uint16_t confirm_num_rows(MenuLayer *ml, uint16_t section, void *ctx) { return 2; }
// 34px row height + GOTHIC_24_BOLD text: matches timer's itemized "detail"
// menu exactly (dl_cell_height/dl_draw_row, pebble-another-timer/src/c/main.c:1197,1253).
static int16_t confirm_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) { return 34; }
static void confirm_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *ctx2) {
  // No manual background/text-color painting here — menu_layer_set_normal_colors
  // / menu_layer_set_highlight_colors (below) already paint the row and set the
  // default text color, exactly like timer's itemized "detail" menu (dl_draw_row,
  // pebble-another-timer/src/c/main.c:1231-1257, which likewise draws only text).
  GRect b = layer_get_bounds(cell_layer);
  graphics_draw_text(ctx, s_confirm_labels[cell_index->row], fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(6, (b.size.h - 26) / 2, b.size.w - 12, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}
static void confirm_select(MenuLayer *ml, MenuIndex *cell_index, void *ctx) {
  int choice = cell_index->row;
  ConfirmChoiceCb cb = s_confirm_cb;
  void *cctx = s_confirm_ctx;
  window_stack_pop(true);
  if (cb) { cb(choice, cctx); }
}
static void confirm_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_confirm_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_confirm_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = confirm_num_rows, .get_cell_height = confirm_cell_height,
    .draw_row = confirm_draw_row, .select_click = confirm_select });
  menu_layer_set_normal_colors(s_confirm_menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_confirm_menu, GColorBlack, GColorWhite);
  menu_layer_set_click_config_onto_window(s_confirm_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_confirm_menu));
}
static void confirm_window_unload(Window *w) {
  menu_layer_destroy(s_confirm_menu); s_confirm_menu = NULL;
}
static void confirm_window_push(const char *label0, const char *label1, ConfirmChoiceCb cb, void *ctx) {
  s_confirm_labels[0] = label0; s_confirm_labels[1] = label1;
  s_confirm_cb = cb; s_confirm_ctx = ctx;
  if (!s_confirm_window) {
    s_confirm_window = window_create();
    window_set_window_handlers(s_confirm_window, (WindowHandlers){
      .load = confirm_window_load, .unload = confirm_window_unload });
  }
  window_stack_push(s_confirm_window, true);
}

// ============================ shared "box-type duration dial" renderer ============================
//
// Ported from pebble-another-timer's non-touch duration dial
// (dial_update_proc / dl_draw_triangle_up_sized / dl_draw_triangle_down_sized,
// src/c/main.c:929-1052): each field is a 3px-bordered box with a big bold
// digit and hand-drawn up/down triangles above/below, the active field's
// border+triangles in black, inactive fields dimmed. Reused by both the time
// editor and the snooze editor below.

#define DIAL_TRI_HW 10
#define DIAL_TRI_H 12

static void dial_draw_triangle_up(GContext *ctx, int cx, int y, GColor c) {
  graphics_context_set_fill_color(ctx, c);
  graphics_context_set_stroke_color(ctx, c);
  for (int dy = 0; dy <= DIAL_TRI_H; dy++) {
    int half = (DIAL_TRI_HW * dy) / DIAL_TRI_H;
    graphics_draw_line(ctx, GPoint(cx - half, y + dy), GPoint(cx + half, y + dy));
  }
  graphics_draw_line(ctx, GPoint(cx - DIAL_TRI_HW, y + DIAL_TRI_H), GPoint(cx, y));
  graphics_draw_line(ctx, GPoint(cx, y), GPoint(cx + DIAL_TRI_HW, y + DIAL_TRI_H));
  graphics_draw_line(ctx, GPoint(cx - DIAL_TRI_HW, y + DIAL_TRI_H), GPoint(cx + DIAL_TRI_HW, y + DIAL_TRI_H));
}

static void dial_draw_triangle_down(GContext *ctx, int cx, int y, GColor c) {
  graphics_context_set_fill_color(ctx, c);
  graphics_context_set_stroke_color(ctx, c);
  for (int dy = 0; dy <= DIAL_TRI_H; dy++) {
    int half = DIAL_TRI_HW - (DIAL_TRI_HW * dy) / DIAL_TRI_H;
    graphics_draw_line(ctx, GPoint(cx - half, y + dy), GPoint(cx + half, y + dy));
  }
  graphics_draw_line(ctx, GPoint(cx - DIAL_TRI_HW, y), GPoint(cx, y + DIAL_TRI_H));
  graphics_draw_line(ctx, GPoint(cx, y + DIAL_TRI_H), GPoint(cx + DIAL_TRI_HW, y));
  graphics_draw_line(ctx, GPoint(cx - DIAL_TRI_HW, y), GPoint(cx + DIAL_TRI_HW, y));
}

// Draws `n` adjacent number boxes spanning `area`, `text[i]` in each, `active`
// highlighted. Vertical text centering uses timer's "rise" correction (GOTHIC/
// BITHAM fonts reserve headroom above the caps, so a measured content box
// still sits low when centered without it). `labels` (nullable, per-box) draws
// a small caption above each box's up-triangle — pass NULL for none (used by
// the time editor, where the header already conveys "hour"/"minute" by
// position); callers that pass labels must reserve `label_h` via
// dial_box_area's `top_label_h` param so the caption doesn't collide with the
// screen header above it.
static void dial_draw_number_boxes(GContext *ctx, GRect area, int n, const char *const *text,
                                    const char *const *labels, int label_h, int active) {
  int gap = 6;
  int bw = (area.size.w - gap * (n - 1)) / n;
  if (bw < 1) { bw = 1; }
  int bh = area.size.h;
  int by = area.origin.y;
  GFont num_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont label_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  for (int i = 0; i < n; i++) {
    int x = area.origin.x + i * (bw + gap);
    GColor c = (i == active) ? GColorBlack : PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack);
    int cx = x + bw / 2;

    if (labels && labels[i]) {
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx, labels[i], label_font, GRect(x, by - 20 - label_h - 10, bw, label_h),
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }

    graphics_context_set_stroke_color(ctx, c);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_rect(ctx, GRect(x, by, bw, bh));

    graphics_context_set_text_color(ctx, GColorBlack);
    int text_h = graphics_text_layout_get_content_size(
      text[i], num_font, GRect(0, 0, bw - 4, 200), GTextOverflowModeFill, GTextAlignmentCenter).h;
    const int rise = 5;
    graphics_draw_text(ctx, text[i], num_font, GRect(x + 2, by + (bh - text_h) / 2 - rise, bw - 4, text_h),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);

    dial_draw_triangle_up(ctx, cx, by - 20, c);
    dial_draw_triangle_down(ctx, cx, by + bh + 8, c);
  }
}

// Box height/position math, identical to dial_update_proc's: a fixed fraction
// of the window height, clamped so the boxes plus their triangles, a 20px top
// gap, and any `top_label_h` (per-box caption space, 0 if none) never overflow
// `window_h`.
static void dial_box_area(int window_h, int top_label_h, int *out_by, int *out_bh) {
  const int dial_extra_h = 20 + 8 + DIAL_TRI_H + top_label_h;
  int bh = (window_h * 28) / 100;
  bh = (bh * 3) / 4;
  int max_bh = window_h - dial_extra_h;
  if (max_bh < 1) { max_bh = 1; }
  if (bh > max_bh) { bh = max_bh; }
  if (bh < 1) { bh = 1; }
  int dial_total_h = bh + dial_extra_h;
  int dial_top_y = (window_h - dial_total_h) / 2;
  *out_by = dial_top_y + top_label_h + 20;
  *out_bh = bh;
}

// ============================ time (hour:minute) editor ============================

static Window *s_time_window;
static Layer *s_time_layer;
static Layer *s_time_bottom_bar;
static uint8_t s_time_hour, s_time_minute;
static int s_time_field;   // 0 = hour, 1 = minute
static void (*s_time_on_confirm)(uint8_t hour, uint8_t minute, void *ctx);
static void (*s_time_on_cancel)(void *ctx);
static void (*s_time_on_chord)(void *ctx);
static void *s_time_ctx;

static void time_layer_update(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);

  // Header: just "Time" -- the boxes below already show the staged value
  // directly, so a redundant formatted-value readout on the header's right
  // side was removed. The boxes always edit the raw 24h hour field for
  // unambiguous +/- stepping, regardless of the system's 12h/24h style.
  GFont hf = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  graphics_draw_text(ctx, "Time", hf, GRect(4, 2, b.size.w - 8, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  int margin = 8, gap = 6;
  int bw = (b.size.w - margin * 2 - gap) / 2;
  int by, bh;
  dial_box_area(b.size.h, 0, &by, &bh);

  char hbuf[4], mbuf[4];
  snprintf(hbuf, sizeof(hbuf), "%d", s_time_hour);
  snprintf(mbuf, sizeof(mbuf), "%02d", s_time_minute);
  const char *texts[2] = { hbuf, mbuf };
  dial_draw_number_boxes(ctx, GRect(margin, by, bw * 2 + gap, bh), 2, texts, NULL, 0, s_time_field);

  // Hint for the secret long-press-SELECT cron entry point, drawn in
  // whatever room is left below the boxes -- same font as the main list's
  // one-alarm hint (main_hint_update_proc), horizontally centered.
  if (s_time_on_chord) {
    int dial_bottom = by + bh + 8 + DIAL_TRI_H;
    int hint_h = b.size.h - dial_bottom;
    if (hint_h > 10) {
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx, "Long-press select\nto enter cron mode",
                         fonts_get_system_font(FONT_KEY_GOTHIC_24),
                         GRect(4, dial_bottom, b.size.w - 8, hint_h),
                         GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
  }
}

static void time_adjust(int delta) {
  if (s_time_field == 0) { s_time_hour = (uint8_t)((s_time_hour + 24 + delta) % 24); }
  else { s_time_minute = (uint8_t)((s_time_minute + 60 + delta) % 60); }
  layer_mark_dirty(s_time_layer);
}
static void time_up(ClickRecognizerRef r, void *ctx) { time_adjust(1); }
static void time_down(ClickRecognizerRef r, void *ctx) { time_adjust(-1); }
static void time_select(ClickRecognizerRef r, void *ctx) {
  if (s_time_field == 0) { s_time_field = 1; layer_mark_dirty(s_time_layer); return; }
  uint8_t h = s_time_hour, m = s_time_minute;
  void *c = s_time_ctx;
  void (*cb)(uint8_t, uint8_t, void *) = s_time_on_confirm;
  window_stack_pop(true);
  if (cb) { cb(h, m, c); }
}
static void time_back(ClickRecognizerRef r, void *ctx) {
  if (s_time_field == 1) { s_time_field = 0; layer_mark_dirty(s_time_layer); return; }
  void *c = s_time_ctx;
  void (*cb)(void *) = s_time_on_cancel;
  window_stack_pop(true);
  if (cb) { cb(c); }
}
// Secret long-press SELECT: switches the time editor over to cron-syntax
// entry, regardless of which field (hour/minute) is currently focused.
static void time_select_long(ClickRecognizerRef r, void *ctx) {
  void *c = s_time_ctx;
  void (*cb)(void *) = s_time_on_chord;
  if (!cb) { return; }
  window_stack_pop(true);
  cb(c);
}
static void time_click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 70, time_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 70, time_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, time_select);
  window_single_click_subscribe(BUTTON_ID_BACK, time_back);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, time_select_long, NULL);
}
static void time_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);
  GRect content_bounds = bounds;
  content_bounds.size.h = bottom_bar_top_for_bounds(bounds);
  s_time_layer = layer_create(content_bounds);
  layer_set_update_proc(s_time_layer, time_layer_update);
  layer_add_child(root, s_time_layer);
  s_time_bottom_bar = bottom_bar_attach(root);
}
static void time_window_unload(Window *w) {
  layer_destroy(s_time_layer); s_time_layer = NULL;
  layer_destroy(s_time_bottom_bar); s_time_bottom_bar = NULL;
}
static void time_edit_window_push(uint8_t hour, uint8_t minute,
    void (*on_confirm)(uint8_t, uint8_t, void *), void (*on_cancel)(void *),
    void (*on_chord)(void *), void *ctx) {
  s_time_hour = hour; s_time_minute = minute; s_time_field = 0;
  s_time_on_confirm = on_confirm; s_time_on_cancel = on_cancel;
  s_time_on_chord = on_chord; s_time_ctx = ctx;
  if (!s_time_window) {
    s_time_window = window_create();
    window_set_window_handlers(s_time_window, (WindowHandlers){
      .load = time_window_load, .unload = time_window_unload });
    window_set_click_config_provider(s_time_window, time_click_config);
  }
  window_stack_push(s_time_window, true);
}

// ============================ cron syntax help ============================
//
// A plain scrollable text screen (ScrollLayer + TextLayer, no MenuLayer)
// explaining the cron field syntax, reached from the cron editor's own
// "Help" row. BACK is left unbound -- unlike the ring screen, the default
// "pop the window" behavior is exactly what's wanted here, so there's no
// custom click config beyond what scroll_layer_set_click_config_onto_window
// already wires up (UP/DOWN scroll).

static Window *s_cron_help_window;
static ScrollLayer *s_cron_help_scroll;
static TextLayer *s_cron_help_text;
static Layer *s_cron_help_header;

#define CRON_HELP_HEADER_H 30

// Same GOTHIC_24 (not bold) used by the time editor's own cron-mode hint --
// see the "secret long-press-SELECT" comment below.
static const char *const CRON_HELP_TEXT =
  "Alarm fires every minute\n"
  "that matches the configured\n"
  "pattern.\n"
  "\n"
  "Example patterns:\n"
  "\n"
  "Wildcard:\n"
  "\"*\" in weekday\n"
  "fires every weekday\n"
  "\n"
  "Range:\n"
  "\"8-16\" in hour\n"
  "fires between 8-16 o'clock\n"
  "\n"
  "List:\n"
  "\"8,16\" in hour\n"
  "fires when hour is 8 or 16\n"
  "\n"
  "Wildcard & step:\n"
  "\"*/20\" in minute\n"
  "fires every 20 minutes\n"
  "\n"
  "Range & step:\n"
  "\"8-16/2\" in hour\n"
  "fires between 8-16 o'clock\n"
  "when hour is 8, 10, 12, etc.";

static void cron_help_header_update_proc(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "Cron help", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(4, 2, b.size.w - 8, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void cron_help_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);

  s_cron_help_header = layer_create(GRect(0, 0, bounds.size.w, CRON_HELP_HEADER_H));
  layer_set_update_proc(s_cron_help_header, cron_help_header_update_proc);
  layer_add_child(root, s_cron_help_header);

  GRect scroll_bounds = GRect(0, CRON_HELP_HEADER_H, bounds.size.w, bounds.size.h - CRON_HELP_HEADER_H);
  s_cron_help_scroll = scroll_layer_create(scroll_bounds);
  scroll_layer_set_click_config_onto_window(s_cron_help_scroll, w);

  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24);
  int text_w = scroll_bounds.size.w - 8;
  GSize sz = graphics_text_layout_get_content_size(
      CRON_HELP_TEXT, f, GRect(0, 0, text_w, 2000), GTextOverflowModeWordWrap, GTextAlignmentLeft);

  s_cron_help_text = text_layer_create(GRect(4, 4, text_w, sz.h + 8));
  text_layer_set_font(s_cron_help_text, f);
  text_layer_set_overflow_mode(s_cron_help_text, GTextOverflowModeWordWrap);
  text_layer_set_text(s_cron_help_text, CRON_HELP_TEXT);
  scroll_layer_add_child(s_cron_help_scroll, text_layer_get_layer(s_cron_help_text));
  scroll_layer_set_content_size(s_cron_help_scroll, GSize(scroll_bounds.size.w, sz.h + 16));

  layer_add_child(root, scroll_layer_get_layer(s_cron_help_scroll));
}

static void cron_help_window_unload(Window *w) {
  scroll_layer_destroy(s_cron_help_scroll); s_cron_help_scroll = NULL;
  text_layer_destroy(s_cron_help_text); s_cron_help_text = NULL;
  layer_destroy(s_cron_help_header); s_cron_help_header = NULL;
}

static void cron_help_window_push(void) {
  if (!s_cron_help_window) {
    s_cron_help_window = window_create();
    window_set_window_handlers(s_cron_help_window, (WindowHandlers){
      .load = cron_help_window_load, .unload = cron_help_window_unload });
  }
  window_stack_push(s_cron_help_window, true);
}

// ============================ cron field editor ============================
//
// One MenuLayer-based window, reused verbatim across every call site that
// needs cron entry (creation wizard, converting an existing normal alarm via
// the chord, re-editing an existing cron alarm) -- callers only differ in
// which on_confirm/on_cancel they pass in, same convention as every other
// editor window in this file.

#define CRON_ROW_SUBMIT  0
#define CRON_ROW_MINUTE  1
#define CRON_ROW_HOUR    2
#define CRON_ROW_DOW     3
#define CRON_ROW_HELP    4
#define CRON_ROW_COUNT   5

static Window *s_cron_window;
static MenuLayer *s_cron_menu;
static Layer *s_cron_bottom_bar;
static char s_cron_min[CRON_FIELD_LEN], s_cron_hour[CRON_FIELD_LEN], s_cron_dow[CRON_FIELD_LEN];
static void (*s_cron_on_confirm)(const char *min_str, const char *hour_str, const char *dow_str, void *ctx);
static void (*s_cron_on_cancel)(void *ctx);
static void *s_cron_ctx;

static uint16_t cron_num_rows(MenuLayer *ml, uint16_t section, void *ctx) { return CRON_ROW_COUNT; }
static int16_t cron_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) { return 34; }

// A field's raw text is validated live (for the "(invalid)" annotation) but
// never blocks typing -- only Submit is gated on all three parsing cleanly.
static bool cron_field_valid(const char *text, int min_val, int max_val) {
  uint64_t mask;
  return ac_cron_parse_field(text, min_val, max_val, &mask);
}

static void cron_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *ctx2) {
  GRect b = layer_get_bounds(cell_layer);
  const char *key = "";
  char value[CRON_FIELD_LEN + 12] = "";
  switch (cell_index->row) {
    case CRON_ROW_SUBMIT:
      graphics_draw_text(ctx, "Apply", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                         GRect(6, (b.size.h - 26) / 2, b.size.w - 12, 26),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      return;
    case CRON_ROW_MINUTE:
      key = "Minute";
      snprintf(value, sizeof(value), "%s%s", s_cron_min, cron_field_valid(s_cron_min, 0, 59) ? "" : " (invalid)");
      break;
    case CRON_ROW_HOUR:
      key = "Hour";
      snprintf(value, sizeof(value), "%s%s", s_cron_hour, cron_field_valid(s_cron_hour, 0, 23) ? "" : " (invalid)");
      break;
    case CRON_ROW_DOW:
      key = "Weekday";
      snprintf(value, sizeof(value), "%s%s", s_cron_dow, cron_field_valid(s_cron_dow, 0, 6) ? "" : " (invalid)");
      break;
    case CRON_ROW_HELP:
      graphics_draw_text(ctx, "Help", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                         GRect(6, (b.size.h - 26) / 2, b.size.w - 12, 26),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      return;
  }
  graphics_draw_text(ctx, key, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(6, (b.size.h - 26) / 2, b.size.w - 12, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, value, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(6, (b.size.h - 26) / 2, b.size.w - 12, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

static void cron_field_done(const char *text, void *ctx) {
  int row = (int)(intptr_t)ctx;
  if (text) {
    char *dst = (row == CRON_ROW_MINUTE) ? s_cron_min : (row == CRON_ROW_HOUR) ? s_cron_hour : s_cron_dow;
    strncpy(dst, text, CRON_FIELD_LEN - 1);
    dst[CRON_FIELD_LEN - 1] = '\0';
  }
  if (s_cron_menu) { menu_layer_reload_data(s_cron_menu); }
}

static void cron_submit(void) {
  if (!cron_field_valid(s_cron_min, 0, 59)) { return; }
  if (!cron_field_valid(s_cron_hour, 0, 23)) { return; }
  if (!cron_field_valid(s_cron_dow, 0, 6)) { return; }
  char min_str[CRON_FIELD_LEN], hour_str[CRON_FIELD_LEN], dow_str[CRON_FIELD_LEN];
  strncpy(min_str, s_cron_min, sizeof(min_str)); min_str[sizeof(min_str) - 1] = '\0';
  strncpy(hour_str, s_cron_hour, sizeof(hour_str)); hour_str[sizeof(hour_str) - 1] = '\0';
  strncpy(dow_str, s_cron_dow, sizeof(dow_str)); dow_str[sizeof(dow_str) - 1] = '\0';
  void *c = s_cron_ctx;
  void (*cb)(const char *, const char *, const char *, void *) = s_cron_on_confirm;
  window_stack_pop(true);
  if (cb) { cb(min_str, hour_str, dow_str, c); }
}

static void cron_select(MenuLayer *ml, MenuIndex *cell_index, void *ctx) {
  switch (cell_index->row) {
    case CRON_ROW_SUBMIT: cron_submit(); break;
    case CRON_ROW_MINUTE:
      multitap_keyboard_window_push_numeric(cron_field_done, s_cron_min, CRON_FIELD_LEN - 1, (void *)(intptr_t)CRON_ROW_MINUTE);
      break;
    case CRON_ROW_HOUR:
      multitap_keyboard_window_push_numeric(cron_field_done, s_cron_hour, CRON_FIELD_LEN - 1, (void *)(intptr_t)CRON_ROW_HOUR);
      break;
    case CRON_ROW_DOW:
      multitap_keyboard_window_push_numeric(cron_field_done, s_cron_dow, CRON_FIELD_LEN - 1, (void *)(intptr_t)CRON_ROW_DOW);
      break;
    case CRON_ROW_HELP:
      cron_help_window_push();
      break;
  }
}

// BACK follows the same convention as time/repeat/snooze editors: on_cancel
// != NULL means "abort the whole flow" (call it, discard staged fields);
// NULL means "revert to the snapshot this screen was opened with, then
// still call on_confirm with that unchanged snapshot" so callers reusing
// this window for in-place editing never have to special-case cancel.
static char s_cron_snapshot_min[CRON_FIELD_LEN], s_cron_snapshot_hour[CRON_FIELD_LEN], s_cron_snapshot_dow[CRON_FIELD_LEN];
static void cron_back(ClickRecognizerRef r, void *ctx) {
  if (s_cron_on_cancel) {
    void *c = s_cron_ctx;
    void (*cb)(void *) = s_cron_on_cancel;
    window_stack_pop(true);
    if (cb) { cb(c); }
    return;
  }
  char min_str[CRON_FIELD_LEN], hour_str[CRON_FIELD_LEN], dow_str[CRON_FIELD_LEN];
  strncpy(min_str, s_cron_snapshot_min, sizeof(min_str)); min_str[sizeof(min_str) - 1] = '\0';
  strncpy(hour_str, s_cron_snapshot_hour, sizeof(hour_str)); hour_str[sizeof(hour_str) - 1] = '\0';
  strncpy(dow_str, s_cron_snapshot_dow, sizeof(dow_str)); dow_str[sizeof(dow_str) - 1] = '\0';
  void *c = s_cron_ctx;
  void (*cb)(const char *, const char *, const char *, void *) = s_cron_on_confirm;
  window_stack_pop(true);
  if (cb) { cb(min_str, hour_str, dow_str, c); }
}
// Long-press SELECT submits outright regardless of cursor position; long-
// press BACK is an explicit no-op (matches the repeat editor's convention).
static void cron_select_long(ClickRecognizerRef r, void *ctx) { cron_submit(); }
static void cron_back_long(ClickRecognizerRef r, void *ctx) { }
static void cron_select_click(ClickRecognizerRef r, void *ctx) {
  MenuIndex idx = menu_layer_get_selected_index(s_cron_menu);
  cron_select(s_cron_menu, &idx, ctx);
}
// single_repeating_click (not a plain single_click) so holding UP/DOWN
// scrolls the menu continuously instead of moving one row per press.
static void cron_up_click(ClickRecognizerRef r, void *ctx) {
  menu_layer_set_selected_next(s_cron_menu, true, MenuRowAlignCenter, true);
}
static void cron_down_click(ClickRecognizerRef r, void *ctx) {
  menu_layer_set_selected_next(s_cron_menu, false, MenuRowAlignCenter, true);
}
// A fully custom provider (not menu_layer_set_click_config_onto_window,
// which owns the whole window's click config and leaves no room to also
// intercept BACK) so this window can implement the on_cancel/snapshot-
// revert BACK convention shared with every other editor in this file.
static void cron_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, cron_select_click);
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, cron_up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, cron_down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, cron_back);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, cron_select_long, NULL);
  window_long_click_subscribe(BUTTON_ID_BACK, 500, cron_back_long, NULL);
}
// Centered title, same style as the "Time"/"Repeat" editor headers.
static Layer *s_cron_header;
#define CRON_HEADER_H 30
static void cron_header_update_proc(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "Cron", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(4, 2, b.size.w - 8, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}
static void cron_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);
  s_cron_header = layer_create(GRect(0, 0, bounds.size.w, CRON_HEADER_H));
  layer_set_update_proc(s_cron_header, cron_header_update_proc);
  layer_add_child(root, s_cron_header);
  GRect menu_bounds = GRect(0, CRON_HEADER_H, bounds.size.w,
                            bottom_bar_top_for_bounds(bounds) - CRON_HEADER_H);
  s_cron_menu = menu_layer_create(menu_bounds);
  menu_layer_set_callbacks(s_cron_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = cron_num_rows, .get_cell_height = cron_cell_height,
    .draw_row = cron_draw_row, .select_click = cron_select });
  menu_layer_set_normal_colors(s_cron_menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_cron_menu, GColorBlack, GColorWhite);
  layer_add_child(root, menu_layer_get_layer(s_cron_menu));
  window_set_click_config_provider(w, cron_click_config);
  s_cron_bottom_bar = bottom_bar_attach(root);
}
static void cron_window_unload(Window *w) {
  menu_layer_destroy(s_cron_menu); s_cron_menu = NULL;
  layer_destroy(s_cron_header); s_cron_header = NULL;
  layer_destroy(s_cron_bottom_bar); s_cron_bottom_bar = NULL;
}
static void cron_edit_window_push(const char *min_str, const char *hour_str, const char *dow_str,
    void (*on_confirm)(const char *min_str, const char *hour_str, const char *dow_str, void *ctx),
    void (*on_cancel)(void *ctx), void *ctx) {
  strncpy(s_cron_min, min_str ? min_str : "*", CRON_FIELD_LEN - 1); s_cron_min[CRON_FIELD_LEN - 1] = '\0';
  strncpy(s_cron_hour, hour_str ? hour_str : "*", CRON_FIELD_LEN - 1); s_cron_hour[CRON_FIELD_LEN - 1] = '\0';
  strncpy(s_cron_dow, dow_str ? dow_str : "*", CRON_FIELD_LEN - 1); s_cron_dow[CRON_FIELD_LEN - 1] = '\0';
  strncpy(s_cron_snapshot_min, s_cron_min, CRON_FIELD_LEN);
  strncpy(s_cron_snapshot_hour, s_cron_hour, CRON_FIELD_LEN);
  strncpy(s_cron_snapshot_dow, s_cron_dow, CRON_FIELD_LEN);
  s_cron_on_confirm = on_confirm; s_cron_on_cancel = on_cancel; s_cron_ctx = ctx;
  if (!s_cron_window) {
    s_cron_window = window_create();
    window_set_window_handlers(s_cron_window, (WindowHandlers){
      .load = cron_window_load, .unload = cron_window_unload });
  }
  window_stack_push(s_cron_window, true);
  // The window (and its MenuLayer) is cached and reused across every call
  // site, so a prior open's scroll position would otherwise leak into this
  // one -- always land back on "Apply" (row 0) when (re)opening.
  if (s_cron_menu) {
    menu_layer_set_selected_index(s_cron_menu, (MenuIndex){ 0, CRON_ROW_SUBMIT }, MenuRowAlignTop, false);
  }
}

// ============================ repeat / weekday editor ============================
//
// An itemized MenuLayer, same shape as the cron editor: row 0 is "Apply",
// rows 1-7 are the individual weekdays starting at the configured
// first-day-of-week, each an independent on/off row. There is no separate
// "Repeat" on/off item any more -- "repeats" isn't something the user sets
// directly here, it's derived on Apply purely from whether any weekday ended
// up picked: any day picked -> recurring weekly on those days; no days
// picked -> a one-time alarm for the next occurrence of the time, with no
// specific weekday. This deliberately drops the old "pick exactly one day,
// leave repeat off" one-time-on-a-specific-day state.

#define REPEAT_ROW_SUBMIT  0
#define REPEAT_ROW_COUNT   8   // Apply + 7 weekdays

static Window *s_repeat_window;
static MenuLayer *s_repeat_menu;
static uint8_t s_repeat_days;             // which day(s) are picked
static uint8_t s_repeat_days_snapshot;    // value this screen was opened with -- restored on cancel
static void (*s_repeat_on_confirm)(bool repeats, uint8_t repeat_days, void *ctx);
static void (*s_repeat_on_cancel)(void *ctx);   // NULL: revert-and-confirm (edit flow); set: abort (wizard)
static void *s_repeat_ctx;

// Full weekday names, indexed by wday (0=Sunday..6=Saturday).
static const char *DAY_NAME[7] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

static uint16_t repeat_num_rows(MenuLayer *ml, uint16_t section, void *ctx) { return REPEAT_ROW_COUNT; }
static int16_t repeat_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) { return 34; }

static void repeat_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *ctx2) {
  GRect b = layer_get_bounds(cell_layer);
  if (cell_index->row == REPEAT_ROW_SUBMIT) {
    graphics_draw_text(ctx, "Apply", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(6, (b.size.h - 26) / 2, b.size.w - 12, 26),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    return;
  }
  int wday = (s_first_day_of_week + (cell_index->row - 1)) % 7;
  bool on = (s_repeat_days & AC_DAY_BIT(wday)) != 0;
  char text[24];
  snprintf(text, sizeof(text), "[%s] %s", on ? "X" : "  ", DAY_NAME[wday]);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(6, (b.size.h - 26) / 2, b.size.w - 12, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// repeats is derived, never stored independently of repeat_days.
static void repeat_confirm_and_pop(void) {
  uint8_t days = s_repeat_days;
  bool repeats = (days != 0);
  void *c = s_repeat_ctx;
  void (*cb)(bool, uint8_t, void *) = s_repeat_on_confirm;
  window_stack_pop(true);
  if (cb) { cb(repeats, days, c); }
}
static void repeat_select(MenuLayer *ml, MenuIndex *cell_index, void *ctx) {
  if (cell_index->row == REPEAT_ROW_SUBMIT) { repeat_confirm_and_pop(); return; }
  int wday = (s_first_day_of_week + (cell_index->row - 1)) % 7;
  s_repeat_days ^= AC_DAY_BIT(wday);
  if (s_repeat_menu) { menu_layer_reload_data(s_repeat_menu); }
}
// BACK follows the same on_cancel-vs-snapshot-revert convention as every
// other editor in this file: on_cancel != NULL means "abort the whole flow"
// (the "+ New alarm" wizard); NULL means "revert to the snapshot this screen
// was opened with, then still call on_confirm with that unchanged snapshot".
static void repeat_back(ClickRecognizerRef r, void *ctx) {
  if (s_repeat_on_cancel) {
    void *c = s_repeat_ctx;
    void (*cb)(void *) = s_repeat_on_cancel;
    window_stack_pop(true);
    cb(c);
    return;
  }
  uint8_t days = s_repeat_days_snapshot;
  bool repeats = (days != 0);
  void *c = s_repeat_ctx;
  void (*cb)(bool, uint8_t, void *) = s_repeat_on_confirm;
  window_stack_pop(true);
  if (cb) { cb(repeats, days, c); }
}
// Long-press SELECT submits outright regardless of cursor position;
// long-press BACK is an explicit no-op (matches the cron editor's convention).
static void repeat_select_long(ClickRecognizerRef r, void *ctx) { repeat_confirm_and_pop(); }
static void repeat_back_long(ClickRecognizerRef r, void *ctx) {}
static void repeat_select_click(ClickRecognizerRef r, void *ctx) {
  MenuIndex idx = menu_layer_get_selected_index(s_repeat_menu);
  repeat_select(s_repeat_menu, &idx, ctx);
}
// single_repeating_click (not a plain single_click) so holding UP/DOWN
// scrolls the menu continuously instead of moving one row per press.
static void repeat_up_click(ClickRecognizerRef r, void *ctx) {
  menu_layer_set_selected_next(s_repeat_menu, true, MenuRowAlignCenter, true);
}
static void repeat_down_click(ClickRecognizerRef r, void *ctx) {
  menu_layer_set_selected_next(s_repeat_menu, false, MenuRowAlignCenter, true);
}
static void repeat_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, repeat_select_click);
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, repeat_up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, repeat_down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, repeat_back);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, repeat_select_long, NULL);
  window_long_click_subscribe(BUTTON_ID_BACK, 500, repeat_back_long, NULL);
}
// Centered title, same style as the cron editor's "Cron" header.
static Layer *s_repeat_header;
#define REPEAT_HEADER_H 30
static void repeat_header_update_proc(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "Repeat", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(4, 2, b.size.w - 8, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}
static void repeat_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);
  s_repeat_header = layer_create(GRect(0, 0, bounds.size.w, REPEAT_HEADER_H));
  layer_set_update_proc(s_repeat_header, repeat_header_update_proc);
  layer_add_child(root, s_repeat_header);
  GRect menu_bounds = GRect(0, REPEAT_HEADER_H, bounds.size.w, bounds.size.h - REPEAT_HEADER_H);
  s_repeat_menu = menu_layer_create(menu_bounds);
  menu_layer_set_callbacks(s_repeat_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = repeat_num_rows, .get_cell_height = repeat_cell_height,
    .draw_row = repeat_draw_row, .select_click = repeat_select });
  menu_layer_set_normal_colors(s_repeat_menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_repeat_menu, GColorBlack, GColorWhite);
  layer_add_child(root, menu_layer_get_layer(s_repeat_menu));
  window_set_click_config_provider(w, repeat_click_config);
}
static void repeat_window_unload(Window *w) {
  menu_layer_destroy(s_repeat_menu); s_repeat_menu = NULL;
  layer_destroy(s_repeat_header); s_repeat_header = NULL;
}
static void repeat_edit_window_push(uint8_t repeat_days,
    void (*on_confirm)(bool repeats, uint8_t repeat_days, void *ctx),
    void (*on_cancel)(void *ctx), void *ctx) {
  s_repeat_days = repeat_days;
  s_repeat_days_snapshot = repeat_days;
  s_repeat_on_confirm = on_confirm; s_repeat_on_cancel = on_cancel; s_repeat_ctx = ctx;
  if (!s_repeat_window) {
    s_repeat_window = window_create();
    window_set_window_handlers(s_repeat_window, (WindowHandlers){
      .load = repeat_window_load, .unload = repeat_window_unload });
  }
  window_stack_push(s_repeat_window, true);
  // The window (and its MenuLayer) is cached and reused across every call
  // site, so a prior open's scroll position would otherwise leak into this
  // one -- always land back on "Apply" (row 0) when (re)opening.
  if (s_repeat_menu) {
    menu_layer_set_selected_index(s_repeat_menu, (MenuIndex){ 0, REPEAT_ROW_SUBMIT }, MenuRowAlignTop, false);
  }
}

// ============================ snooze (duration + max) editor ============================
//
// A plain custom Layer (like the time editor), not a MenuLayer: this window
// needs UP/DOWN to adjust whichever of its two fields is active, which would
// conflict with MenuLayer's own internal use of UP/DOWN for row-scrolling —
// its ClickConfigProvider owns UP/DOWN/SELECT outright once installed, so a
// MenuLayer-based screen can't also repurpose UP/DOWN as value adjusters.

static Window *s_snooze_window;
static Layer *s_snooze_layer;
static uint16_t s_snooze_minutes_edit;
static uint8_t s_snooze_max_edit;
static uint16_t s_snooze_minutes_original;   // snapshot at open, restored on cancel
static uint8_t s_snooze_max_original;
static int s_snooze_field;   // 0 = duration, 1 = max count
static void (*s_snooze_on_confirm)(uint16_t minutes, uint8_t max_count, void *ctx);
static void (*s_snooze_on_cancel)(void *ctx);   // NULL: revert-and-confirm (edit flow); set: true abort (wizard)
static void *s_snooze_ctx;

// Same box-type dial rendering as the time editor (dial_draw_number_boxes/
// dial_box_area above); the header shows the active field's own label+value
// since duration (minutes) and max-count are different units with no single
// combined "full value" the way timer's H:M:S header has.
static void snooze_layer_update(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);

  GFont hf = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  graphics_draw_text(ctx, "Snooze", hf, GRect(4, 2, b.size.w - 8, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  int margin = 8, gap = 6;
  int bw = (b.size.w - margin * 2 - gap) / 2;
  const int label_h = 20;   // taller box to fit the bold, one-step-larger caption font
  int by, bh;
  dial_box_area(b.size.h, label_h, &by, &bh);

  char dbuf[10], mbuf[10];
  if (s_snooze_minutes_edit == 0) { snprintf(dbuf, sizeof(dbuf), "Disabled"); }
  else { snprintf(dbuf, sizeof(dbuf), "%d", s_snooze_minutes_edit); }
  if (s_snooze_max_edit == 0) { snprintf(mbuf, sizeof(mbuf), "Infinite"); }
  else { snprintf(mbuf, sizeof(mbuf), "%d", s_snooze_max_edit); }
  const char *texts[2] = { dbuf, mbuf };
  const char *labels[2] = { "Minutes", "Repeats" };
  dial_draw_number_boxes(ctx, GRect(margin, by, bw * 2 + gap, bh), 2, texts, labels, label_h, s_snooze_field);
}

static void snooze_up(ClickRecognizerRef r, void *ctx) {
  if (s_snooze_field == 0) { if (s_snooze_minutes_edit < 60) { s_snooze_minutes_edit++; } }
  else { if (s_snooze_max_edit < 20) { s_snooze_max_edit++; } }
  layer_mark_dirty(s_snooze_layer);
}
static void snooze_down(ClickRecognizerRef r, void *ctx) {
  if (s_snooze_field == 0) { if (s_snooze_minutes_edit > 0) { s_snooze_minutes_edit--; } }
  else { if (s_snooze_max_edit > 0) { s_snooze_max_edit--; } }
  layer_mark_dirty(s_snooze_layer);
}
// SELECT advances field 0 -> 1, and confirms (submits) when already on the
// last field — same pattern as the time editor's time_select.
static void snooze_select(ClickRecognizerRef r, void *ctx) {
  if (s_snooze_field == 0) { s_snooze_field = 1; layer_mark_dirty(s_snooze_layer); return; }
  uint16_t minutes = s_snooze_minutes_edit;
  uint8_t max_count = s_snooze_max_edit;
  void *c = s_snooze_ctx;
  void (*cb)(uint16_t, uint8_t, void *) = s_snooze_on_confirm;
  window_stack_pop(true);
  if (cb) { cb(minutes, max_count, c); }
}
// BACK retreats field 1 -> 0; on field 0 (leftmost), it cancels. If the
// caller supplied an on_cancel (the "+ New alarm" wizard — see
// start_new_alarm_flow), that aborts the whole flow outright. Otherwise (the
// per-alarm edit menu, editing one field of an already-existing alarm) it
// reverts to the values this screen was opened with and still calls
// on_confirm, so that caller doesn't need to special-case "cancel" at all.
static void snooze_back(ClickRecognizerRef r, void *ctx) {
  if (s_snooze_field == 1) { s_snooze_field = 0; layer_mark_dirty(s_snooze_layer); return; }
  if (s_snooze_on_cancel) {
    void *c = s_snooze_ctx;
    void (*cb)(void *) = s_snooze_on_cancel;
    window_stack_pop(true);
    cb(c);
    return;
  }
  uint16_t minutes = s_snooze_minutes_original;
  uint8_t max_count = s_snooze_max_original;
  void *c = s_snooze_ctx;
  void (*cb)(uint16_t, uint8_t, void *) = s_snooze_on_confirm;
  window_stack_pop(true);
  if (cb) { cb(minutes, max_count, c); }
}
static void snooze_click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 70, snooze_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 70, snooze_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, snooze_select);
  window_single_click_subscribe(BUTTON_ID_BACK, snooze_back);
}
static void snooze_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_snooze_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_snooze_layer, snooze_layer_update);
  layer_add_child(root, s_snooze_layer);
}
static void snooze_window_unload(Window *w) {
  layer_destroy(s_snooze_layer); s_snooze_layer = NULL;
}
static void snooze_edit_window_push(uint16_t minutes, uint8_t max_count,
    void (*on_confirm)(uint16_t minutes, uint8_t max_count, void *ctx),
    void (*on_cancel)(void *ctx), void *ctx) {
  s_snooze_minutes_edit = minutes; s_snooze_max_edit = max_count; s_snooze_field = 0;
  s_snooze_minutes_original = minutes; s_snooze_max_original = max_count;
  s_snooze_on_confirm = on_confirm; s_snooze_on_cancel = on_cancel; s_snooze_ctx = ctx;
  if (!s_snooze_window) {
    s_snooze_window = window_create();
    window_set_window_handlers(s_snooze_window, (WindowHandlers){
      .load = snooze_window_load, .unload = snooze_window_unload });
    window_set_click_config_provider(s_snooze_window, snooze_click_config);
  }
  window_stack_push(s_snooze_window, true);
}

// ================================= alarm CRUD =================================

static void remove_alarm_at(int idx) {
  if (idx < 0 || idx >= s_count) { return; }
  // snooze_until/snooze_count live on the Alarm itself now, so this shift
  // carries them along automatically — no separate bookkeeping to shift.
  for (int i = idx; i < s_count - 1; i++) {
    s_alarms[i] = s_alarms[i + 1];
  }
  s_count--;
}

// ================================= edit menu (existing alarm) =================================

static Window *s_edit_window;
static MenuLayer *s_edit_menu;
static Layer *s_edit_bottom_bar;
static int s_edit_idx = -1;

static const char *vibe_pattern_name(uint8_t pattern) {
  switch (pattern) {
    case 1: return "Short";
    case 2: return "Long";
    default: return "Double";
  }
}

#define EDIT_ROW_LABEL    0
#define EDIT_ROW_ENABLE   1
#define EDIT_ROW_TIME     2
#define EDIT_ROW_REPEAT   3
#define EDIT_ROW_CRON     4
#define EDIT_ROW_SNOOZE   5
#define EDIT_ROW_VIBE     6
#define EDIT_ROW_VIBE_PATTERN 7
#define EDIT_ROW_SOUND    8
#define EDIT_ROW_INCREASING_VOLUME 9
#define EDIT_ROW_AUTO_STOP 10
#define EDIT_ROW_DELETE   11
#define EDIT_ROW_MAX_COUNT 11   // LABEL/ENABLE/TIME/REPEAT/SNOOZE/VIBE/VIBE_PATTERN/SOUND/INCREASING_VOLUME/AUTO_STOP/DELETE

// Builds the ordered list of row "kinds" for the current alarm's mode into
// out_kinds (capacity EDIT_ROW_MAX_COUNT) and returns how many are used.
// Collapses EDIT_ROW_TIME + EDIT_ROW_REPEAT into one EDIT_ROW_CRON slot when
// is_cron, so cell_index->row still maps 1:1 to a list position without
// scattering is_cron checks through draw/select.
static int edit_build_rows(int *out_kinds, bool is_cron) {
  int n = 0;
  out_kinds[n++] = EDIT_ROW_DELETE;
  if (is_cron) {
    out_kinds[n++] = EDIT_ROW_CRON;
  } else {
    out_kinds[n++] = EDIT_ROW_TIME;
  }
  out_kinds[n++] = EDIT_ROW_LABEL;
  out_kinds[n++] = EDIT_ROW_ENABLE;
  if (!is_cron) {
    out_kinds[n++] = EDIT_ROW_REPEAT;
  }
  out_kinds[n++] = EDIT_ROW_SNOOZE;
  out_kinds[n++] = EDIT_ROW_VIBE;
  out_kinds[n++] = EDIT_ROW_VIBE_PATTERN;
  out_kinds[n++] = EDIT_ROW_SOUND;
  out_kinds[n++] = EDIT_ROW_INCREASING_VOLUME;
  out_kinds[n++] = EDIT_ROW_AUTO_STOP;
  return n;
}

static uint16_t edit_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  int kinds[EDIT_ROW_MAX_COUNT];
  bool is_cron = (s_edit_idx >= 0 && s_edit_idx < s_count) && s_alarms[s_edit_idx].is_cron;
  return (uint16_t)edit_build_rows(kinds, is_cron);
}
// 34px row height, GOTHIC_24_BOLD text: matches timer's itemized "detail"
// menu row (dl_cell_height/dl_draw_row, pebble-another-timer/src/c/
// main.c:1197,1253). No header row — the label lives in its own menu entry
// instead (edit_draw_header used to show name+time up top; removed so every
// field, including the label, is a plain itemized row).
static int16_t edit_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) { return 34; }

// Key (left-aligned) + value (right-aligned) per row, no colons — the two
// draw calls share the same rect and just differ in alignment, matching a
// standard settings-row layout.
static void edit_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *ctx2) {
  if (s_edit_idx < 0 || s_edit_idx >= s_count) { return; }
  const Alarm *a = &s_alarms[s_edit_idx];
  int kinds[EDIT_ROW_MAX_COUNT];
  edit_build_rows(kinds, a->is_cron);
  GRect b = layer_get_bounds(cell_layer);
  const char *key = "";
  char value[48] = "";
  switch (kinds[cell_index->row]) {
    case EDIT_ROW_LABEL:
      key = "Label";
      snprintf(value, sizeof(value), "%s", a->name[0] ? a->name : "<none>");
      break;
    case EDIT_ROW_ENABLE:
      key = "State";
      if (a->enabled && (a->repeats || a->is_cron) && a->skip_next) {
        snprintf(value, sizeof(value), "%s", "Skip next");
      } else {
        snprintf(value, sizeof(value), "%s", a->enabled ? "Enabled" : "Disabled");
      }
      break;
    case EDIT_ROW_TIME: {
      key = "Time";
      char t[16];
      ac_format_time(t, sizeof(t), a->hour, a->minute, clock_is_24h_style());
      snprintf(value, sizeof(value), "%s", t);
      break;
    }
    case EDIT_ROW_REPEAT:
      key = "Repeat";
      ac_format_repeat_summary(value, sizeof(value), a->repeats, a->repeat_days, s_first_day_of_week);
      break;
    case EDIT_ROW_CRON:
      key = "Cron";
      ac_format_cron_summary(value, sizeof(value), a->cron_min, a->cron_hour, a->cron_dow);
      break;
    case EDIT_ROW_SNOOZE:
      key = "Snooze";
      if (a->snooze_minutes == 0) { snprintf(value, sizeof(value), "Disabled"); }
      else if (a->snooze_max == 0) { snprintf(value, sizeof(value), "%d min, unlimited", a->snooze_minutes); }
      else { snprintf(value, sizeof(value), "%d min, %d rep", a->snooze_minutes, a->snooze_max); }
      break;
    case EDIT_ROW_VIBE:
      key = "Vibration";
      snprintf(value, sizeof(value), "%s", a->vibration_enabled ? "On" : "Off");
      break;
    case EDIT_ROW_VIBE_PATTERN:
      key = "Vibe pattern";
      snprintf(value, sizeof(value), "%s", vibe_pattern_name(a->vibe_pattern));
      break;
    case EDIT_ROW_SOUND:
      key = "Sound";
      snprintf(value, sizeof(value), "%s", a->sound_enabled ? "On" : "Off");
      break;
    case EDIT_ROW_INCREASING_VOLUME:
      key = "Increasing volume";
      snprintf(value, sizeof(value), "%s", a->increasing_volume ? "On" : "Off");
      break;
    case EDIT_ROW_AUTO_STOP:
      key = "Auto-stop";
      snprintf(value, sizeof(value), "%s", a->auto_stop ? "On" : "Off");
      break;
    case EDIT_ROW_DELETE:
      key = "Delete";   // action row, no value
      break;
    default: break;
  }
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GRect row = GRect(6, (b.size.h - 26) / 2, b.size.w - 12, 26);
  graphics_draw_text(ctx, key, f, row, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (value[0]) {
    graphics_draw_text(ctx, value, f, row, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }
}

static void edit_close(void) {
  window_stack_remove(s_edit_window, true);
}

// Whenever a schedule-affecting field (hour, minute, repeats, repeat_days,
// or re-enabling) changes, `last_fired_day` needs recomputing — otherwise
// ac_is_due can fire the alarm the instant the app is next opened, even
// when the new schedule's real next occurrence isn't today. ac_is_due has
// no notion of "next occurrence": it only checks "time-of-day passed" +
// "not already fired today", so for a repeat_days==0 (no specific day)
// alarm, or a repeating/specific-weekday alarm whose set time has already
// passed today, it can't tell "genuinely due" apart from "edited to an
// already-past time, should wait for its real next occurrence" — both look
// identical to it (time passed, never fired today). ac_next_offset_days
// already computes that real next occurrence correctly (0 = today, since
// its time hasn't passed yet; anything else = a future day), so mirror its
// verdict here: block today (stamp last_fired_day to today) unless today
// genuinely is next.
static void resync_last_fired_for_schedule_change(Alarm *a) {
  if (!a->enabled) { return; }
  int wday, hour, min;
  now_wall(&wday, &hour, &min);
  int off = ac_next_offset_days(a, wday, hour, min);
  a->last_fired_day = (off == 0) ? -1 : now_day_id();
}

static void edit_on_label_done(const char *text, void *ctx) {
  if (text && s_edit_idx >= 0 && s_edit_idx < s_count) {
    strncpy(s_alarms[s_edit_idx].name, text, NAME_LEN);
    s_alarms[s_edit_idx].name[NAME_LEN] = '\0';
    persist_all(); reload_ui();
  }
  if (s_edit_menu) { menu_layer_reload_data(s_edit_menu); }
}

static void edit_on_time_confirm(uint8_t hour, uint8_t minute, void *ctx) {
  if (s_edit_idx >= 0 && s_edit_idx < s_count) {
    Alarm *a = &s_alarms[s_edit_idx];
    a->hour = hour;
    a->minute = minute;
    resync_last_fired_for_schedule_change(a);
    persist_all(); rearm_wakeup(); reload_ui();
  }
  if (s_edit_menu) { menu_layer_reload_data(s_edit_menu); }
}

// edit_on_cron_confirm serves two call sites: converting an existing normal
// alarm to cron (chord on its Time row) and re-editing an existing cron
// alarm (its Cron row) -- both just need the alarm's schedule fields
// replaced with a fresh parse, so one function handles both.
static void edit_on_cron_confirm(const char *min_str, const char *hour_str, const char *dow_str, void *ctx) {
  if (s_edit_idx >= 0 && s_edit_idx < s_count) {
    Alarm *a = &s_alarms[s_edit_idx];
    a->is_cron = true;
    a->repeats = false;
    // Defensive fallback to "*"/full-mask on a parse failure -- Submit
    // already validated in cron_edit_window_push, so this only guards
    // against ever persisting an unparseable field.
    if (!ac_cron_parse_field(min_str, 0, 59, &a->cron_min_mask)) { ac_cron_parse_field("*", 0, 59, &a->cron_min_mask); }
    uint64_t hour_mask64 = 0;
    if (!ac_cron_parse_field(hour_str, 0, 23, &hour_mask64)) { ac_cron_parse_field("*", 0, 23, &hour_mask64); }
    a->cron_hour_mask = (uint32_t)hour_mask64;
    uint64_t dow_mask = 0;
    if (!ac_cron_parse_field(dow_str, 0, 6, &dow_mask)) { ac_cron_parse_field("*", 0, 6, &dow_mask); }
    a->repeat_days = (uint8_t)dow_mask;
    strncpy(a->cron_min, min_str, CRON_FIELD_LEN - 1); a->cron_min[CRON_FIELD_LEN - 1] = '\0';
    strncpy(a->cron_hour, hour_str, CRON_FIELD_LEN - 1); a->cron_hour[CRON_FIELD_LEN - 1] = '\0';
    strncpy(a->cron_dow, dow_str, CRON_FIELD_LEN - 1); a->cron_dow[CRON_FIELD_LEN - 1] = '\0';
    // No eager-fire guard needed (see alarm_calc.h's is_cron doc comment):
    // firing right away if the freshly-edited pattern currently matches is
    // correct multi-fire cron behavior, not a bug to suppress.
    a->cron_last_fired_min = -1;
    persist_all(); rearm_wakeup(); reload_ui();
  }
  if (s_edit_menu) { menu_layer_reload_data(s_edit_menu); }
}
// Chord on an existing NORMAL alarm's Time row: there's no prior cron state
// to revert to, so cancelling must leave the alarm as a normal,
// unconverted alarm -- a real no-op callback, not NULL (NULL would mean
// "revert to the */*/* snapshot", which doesn't apply here).
static void edit_cron_convert_cancel(void *ctx) { }
static void edit_on_time_chord(void *ctx) {
  char min_str[8], hour_str[8];
  snprintf(min_str, sizeof(min_str), "%d", s_time_minute);
  snprintf(hour_str, sizeof(hour_str), "%d", s_time_hour);
  cron_edit_window_push(min_str, hour_str, "*", edit_on_cron_confirm, edit_cron_convert_cancel, NULL);
}

static void edit_on_repeat_confirm(bool repeats, uint8_t repeat_days, void *ctx) {
  if (s_edit_idx >= 0 && s_edit_idx < s_count) {
    Alarm *a = &s_alarms[s_edit_idx];
    a->repeats = repeats;
    a->repeat_days = repeat_days;
    resync_last_fired_for_schedule_change(a);
    persist_all(); rearm_wakeup(); reload_ui();
  }
  if (s_edit_menu) { menu_layer_reload_data(s_edit_menu); }
}

static void edit_on_snooze_confirm(uint16_t minutes, uint8_t max_count, void *ctx) {
  if (s_edit_idx >= 0 && s_edit_idx < s_count) {
    s_alarms[s_edit_idx].snooze_minutes = minutes;
    s_alarms[s_edit_idx].snooze_max = max_count;
    persist_all();
  }
  if (s_edit_menu) { menu_layer_reload_data(s_edit_menu); }
}

static void edit_on_delete_choice(int choice, void *ctx) {
  if (choice != 0) { return; }   // 0 = "Delete", 1 = "Cancel"
  if (s_edit_idx >= 0 && s_edit_idx < s_count) {
    remove_alarm_at(s_edit_idx);
    persist_all(); rearm_wakeup(); reload_ui();
  }
  edit_close();
}

static void edit_select(MenuLayer *ml, MenuIndex *cell_index, void *ctx) {
  if (s_edit_idx < 0 || s_edit_idx >= s_count) { return; }
  Alarm *a = &s_alarms[s_edit_idx];
  int kinds[EDIT_ROW_MAX_COUNT];
  edit_build_rows(kinds, a->is_cron);
  switch (kinds[cell_index->row]) {
    case EDIT_ROW_LABEL:
      multitap_keyboard_window_push_ex(edit_on_label_done, a->name, NAME_LEN, NULL);
      break;
    case EDIT_ROW_ENABLE:
      // Same three-state (or two-state, for a plain one-time alarm) cycle as
      // the main list's short-press — no confirm prompt, since every state
      // is one more press away from undoing itself.
      ml_cycle_alarm_state(s_edit_idx);
      menu_layer_reload_data(s_edit_menu);
      break;
    case EDIT_ROW_TIME:
      time_edit_window_push(a->hour, a->minute, edit_on_time_confirm, NULL, edit_on_time_chord, NULL);
      break;
    case EDIT_ROW_REPEAT:
      repeat_edit_window_push(a->repeat_days, edit_on_repeat_confirm, NULL, NULL);
      break;
    case EDIT_ROW_CRON:
      cron_edit_window_push(a->cron_min, a->cron_hour, a->cron_dow, edit_on_cron_confirm, NULL, NULL);
      break;
    case EDIT_ROW_SNOOZE:
      snooze_edit_window_push(a->snooze_minutes, a->snooze_max, edit_on_snooze_confirm, NULL, NULL);
      break;
    case EDIT_ROW_VIBE:
      a->vibration_enabled = !a->vibration_enabled;
      persist_all();
      menu_layer_reload_data(s_edit_menu);
      break;
    case EDIT_ROW_VIBE_PATTERN:
      // Double -> Short -> Long -> Double, same short-press-cycles
      // convention as the main list's enable/disable/skip state.
      a->vibe_pattern = (a->vibe_pattern + 1) % 3;
      persist_all();
      menu_layer_reload_data(s_edit_menu);
      break;
    case EDIT_ROW_SOUND:
      a->sound_enabled = !a->sound_enabled;
      persist_all();
      menu_layer_reload_data(s_edit_menu);
      break;
    case EDIT_ROW_INCREASING_VOLUME:
      a->increasing_volume = !a->increasing_volume;
      persist_all();
      menu_layer_reload_data(s_edit_menu);
      break;
    case EDIT_ROW_AUTO_STOP:
      a->auto_stop = !a->auto_stop;
      persist_all();
      menu_layer_reload_data(s_edit_menu);
      break;
    case EDIT_ROW_DELETE:
      confirm_window_push("Delete", "Cancel", edit_on_delete_choice, NULL);
      break;
  }
}

static void edit_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);
  GRect menu_bounds = bounds;
  menu_bounds.size.h = bottom_bar_top_for_bounds(bounds);
  s_edit_menu = menu_layer_create(menu_bounds);
  menu_layer_set_callbacks(s_edit_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = edit_num_rows, .get_cell_height = edit_cell_height,
    .draw_row = edit_draw_row, .select_click = edit_select });
  menu_layer_set_normal_colors(s_edit_menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_edit_menu, GColorBlack, GColorWhite);
  menu_layer_set_click_config_onto_window(s_edit_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_edit_menu));
  s_edit_bottom_bar = bottom_bar_attach(root);
}
static void edit_window_unload(Window *w) {
  menu_layer_destroy(s_edit_menu); s_edit_menu = NULL;
  layer_destroy(s_edit_bottom_bar); s_edit_bottom_bar = NULL;
}

static void open_edit_window(int idx) {
  s_edit_idx = idx;
  if (!s_edit_window) {
    s_edit_window = window_create();
    window_set_window_handlers(s_edit_window, (WindowHandlers){
      .load = edit_window_load, .unload = edit_window_unload });
  }
  window_stack_push(s_edit_window, true);
}

// ================================= "+ New alarm" creation flow =================================
//
// Chains time -> repeat -> snooze -> label. Exiting ANY of these four
// screens with BACK (i.e. pressing BACK once already on that screen's first
// field/box, or cancelling the label's multitap keyboard) aborts the whole
// flow — the draft is discarded and no alarm is created. The alarm is only
// actually created when the *last* screen (label) is exited via SELECT.

static Alarm s_draft;

// Shared no-op cancel for every step of the wizard: the draft was never
// appended to s_alarms, so there's nothing to undo — this just lets the
// flow end without creating anything.
static void new_alarm_wizard_cancel(void *ctx) {}

static void new_alarm_label_done(const char *text, void *ctx) {
  if (!text) { return; }   // BACK on the keyboard cancels the whole flow
  strncpy(s_draft.name, text, NAME_LEN);
  s_draft.name[NAME_LEN] = '\0';
  s_draft.id = next_alarm_id();
  // The user may have picked an hour:minute (or a repeat_days==0 "no
  // specific day") that's already passed today — without this, it would
  // otherwise look immediately due the instant the app is next opened
  // instead of waiting for its real next occurrence.
  resync_last_fired_for_schedule_change(&s_draft);
  if (s_count < MAX_ALARMS) {
    s_alarms[s_count] = s_draft;
    s_count++;
  }
  persist_all();
  rearm_wakeup();
  reload_ui();
}

// Snooze is no longer its own wizard screen -- s_draft.snooze_minutes/max
// were already seeded from the phone-configured defaults in
// start_new_alarm_flow, so every new alarm just uses those outright.
static void new_alarm_repeat_confirm(bool repeats, uint8_t repeat_days, void *ctx) {
  s_draft.repeats = repeats;
  s_draft.repeat_days = repeat_days;
  multitap_keyboard_window_push_ex(new_alarm_label_done, "", NAME_LEN, NULL);
}

static void new_alarm_time_confirm(uint8_t hour, uint8_t minute, void *ctx) {
  s_draft.hour = hour;
  s_draft.minute = minute;
  repeat_edit_window_push(s_draft.repeat_days, new_alarm_repeat_confirm,
                          new_alarm_wizard_cancel, NULL);
}

// Chord on the wizard's Time screen: the draft becomes a cron alarm instead,
// and Repeat is skipped entirely (day-of-week now lives in the cron dow
// field) -- straight on to Snooze.
static void new_alarm_cron_confirm(const char *min_str, const char *hour_str, const char *dow_str, void *ctx) {
  s_draft.is_cron = true;
  s_draft.repeats = false;
  ac_cron_parse_field(min_str, 0, 59, &s_draft.cron_min_mask);
  uint64_t hour_mask64 = 0;
  ac_cron_parse_field(hour_str, 0, 23, &hour_mask64);
  s_draft.cron_hour_mask = (uint32_t)hour_mask64;
  uint64_t dow_mask = 0;
  ac_cron_parse_field(dow_str, 0, 6, &dow_mask);
  s_draft.repeat_days = (uint8_t)dow_mask;
  strncpy(s_draft.cron_min, min_str, CRON_FIELD_LEN - 1); s_draft.cron_min[CRON_FIELD_LEN - 1] = '\0';
  strncpy(s_draft.cron_hour, hour_str, CRON_FIELD_LEN - 1); s_draft.cron_hour[CRON_FIELD_LEN - 1] = '\0';
  strncpy(s_draft.cron_dow, dow_str, CRON_FIELD_LEN - 1); s_draft.cron_dow[CRON_FIELD_LEN - 1] = '\0';
  s_draft.cron_last_fired_min = -1;
  multitap_keyboard_window_push_ex(new_alarm_label_done, "", NAME_LEN, NULL);
}
// Unlike edit_on_time_chord (converting an already-existing alarm, which
// keeps the exact staged hour:minute), the "+ New alarm" wizard defaults to
// the next full hour, every day -- a fresh cron alarm is far more likely to
// want "top of the hour" than whatever incidental minute the time editor
// happened to be showing.
static void new_alarm_time_chord(void *ctx) {
  char hour_str[8];
  int next_full_hour = (s_time_minute > 0) ? (s_time_hour + 1) % 24 : s_time_hour;
  snprintf(hour_str, sizeof(hour_str), "%d", next_full_hour);
  cron_edit_window_push("0", hour_str, "*", new_alarm_cron_confirm, new_alarm_wizard_cancel, NULL);
}

static void start_new_alarm_flow(void) {
  memset(&s_draft, 0, sizeof(s_draft));
  s_draft.last_fired_day = -1;   // never fired
  s_draft.enabled = true;
  s_draft.vibration_enabled = s_default_vibration_enabled;
  s_draft.vibe_pattern = (uint8_t)s_default_vibe_pattern;
  s_draft.sound_enabled = s_default_sound_enabled;
  s_draft.increasing_volume = s_default_increasing_volume;
  // 0 here (already the case when the phone's "Enable snooze" toggle is
  // off) reuses the existing "snooze disabled outright" convention, see
  // alarm_snooze().
  s_draft.snooze_minutes = (uint16_t)s_default_snooze_minutes;
  s_draft.snooze_max = (uint8_t)s_default_snooze_max;
  // Default to the next minute, not a fixed time, so the time editor opens
  // already close to "now" instead of always needing to be dialed in.
  int wday, hour, min;
  now_wall(&wday, &hour, &min);
  min++;
  if (min >= 60) { min = 0; hour = (hour + 1) % 24; }
  s_draft.hour = (uint8_t)hour;
  s_draft.minute = (uint8_t)min;
  time_edit_window_push(s_draft.hour, s_draft.minute, new_alarm_time_confirm, new_alarm_wizard_cancel,
                        new_alarm_time_chord, NULL);
}

// ================================= main window =================================
//
// Bottom bar (ported from pebble-another-timer's draw_bottom_bar/
// bottom_bar_rect_for_bounds): a thin divider line + black strip pinned to
// the bottom of the main window, current time on the left (via the system's
// own 12h/24h-aware clock_copy_time_string), the next alarm's due time on
// the right — "Next: HH:MM" if it lands on today's calendar date, "Next:
// <Day> HH:MM" otherwise (including tomorrow — just the weekday
// abbreviation, no separate "Tomorrow" case), or "Next: --" if nothing is
// scheduled.

#define BOTTOM_BAR_H 28

static Layer *s_bottom_bar_layer;

static int16_t bottom_bar_top_for_bounds(GRect bounds) {
  return bounds.size.h - BOTTOM_BAR_H;
}

static GRect bottom_bar_rect_for_bounds(GRect bounds) {
  return GRect(0, bottom_bar_top_for_bounds(bounds), bounds.size.w, BOTTOM_BAR_H);
}

static const char *const BOTTOM_BAR_WDAY_ABBR[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

static void format_next_alarm(char *buf, size_t n) {
  int64_t fire_time;
  int idx;
  if (!compute_next_fire_time(&fire_time, &idx)) {
    snprintf(buf, n, "Next: --");
    return;
  }
  time_t target_t = (time_t)fire_time;
  struct tm target = *localtime(&target_t);
  time_t now_t = time(NULL);
  struct tm today = *localtime(&now_t);

  char time_buf[16];
  ac_format_time(time_buf, sizeof(time_buf), (uint8_t)target.tm_hour, (uint8_t)target.tm_min, clock_is_24h_style());

  if (target.tm_year == today.tm_year && target.tm_yday == today.tm_yday) {
    snprintf(buf, n, "Next: %s", time_buf);
  } else {
    snprintf(buf, n, "Next: %s %s", BOTTOM_BAR_WDAY_ABBR[target.tm_wday], time_buf);
  }
}

static void draw_bottom_bar(GContext *ctx, GRect bounds) {
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(bounds.origin.x, bounds.origin.y),
                     GPoint(bounds.origin.x + bounds.size.w - 1, bounds.origin.y));
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(bounds.origin.x, bounds.origin.y + 1, bounds.size.w, bounds.size.h - 1),
                     0, GCornerNone);

  char left[16];
  clock_copy_time_string(left, sizeof(left));
  char right[32];
  format_next_alarm(right, sizeof(right));

  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  const int th = 28;
  const int ty = bounds.origin.y + (bounds.size.h - th) / 2;
  graphics_context_set_text_color(ctx, GColorWhite);

  // Size the left (clock) box to its actual measured width rather than a
  // fixed guess, and give the right ("Next: ...") box only the remaining
  // space — otherwise a long right-aligned string (e.g. "Next: Tomorrow
  // HH:MM") draws into the same pixels as the clock instead of stopping
  // short of it or ellipsizing.
  GSize left_sz = graphics_text_layout_get_content_size(
    left, f, GRect(0, 0, bounds.size.w, th), GTextOverflowModeFill, GTextAlignmentLeft);
  int left_x = bounds.origin.x + 4;
  int right_x = left_x + left_sz.w + 8;
  int right_w = bounds.origin.x + bounds.size.w - 4 - right_x;
  if (right_w < 0) { right_w = 0; }

  graphics_draw_text(ctx, left, f, GRect(left_x, ty, left_sz.w, th),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, right, f, GRect(right_x, ty, right_w, th),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

static void bottom_bar_update_proc(Layer *l, GContext *ctx) {
  draw_bottom_bar(ctx, layer_get_bounds(l));
}

// Shared by every window that shows the bottom bar (main list, time/repeat/
// cron editors, alarm edit menu): creates and attaches the bar layer to
// `root`, sized/positioned via bottom_bar_rect_for_bounds. Callers must
// separately shrink their own content area to bottom_bar_top_for_bounds()
// so the bar doesn't overlap it -- this helper only adds the bar itself.
static Layer *bottom_bar_attach(Layer *root) {
  GRect bounds = layer_get_bounds(root);
  Layer *bar = layer_create(bottom_bar_rect_for_bounds(bounds));
  layer_set_update_proc(bar, bottom_bar_update_proc);
  layer_add_child(root, bar);
  return bar;
}

// One-alarm hint, adapted from pebble-another-timer's empty_hint_update_proc
// (its s_count == 1 branch): drawn into the blank space below the list's
// two rows (the alarm + trailing "+ New alarm"), centered, word-wrapped,
// with the same GOTHIC optical-rise nudge timer uses. The trailing row is
// shorter than an alarm row (ML_NEW_ROW_H vs. ML_ROW_H — see ml_cell_height),
// so free space starts after one of each, not a uniform 2 * ML_ROW_H.
static void main_hint_update_proc(Layer *layer, GContext *ctx) {
  if (s_count != 1) { return; }
  GRect b = layer_get_bounds(layer);
  if (b.size.h <= 32) { return; }
  const GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24);
  const char *msg = "- Short-press to cycle state\n- Long-press to edit";
  int free_top = ML_ROW_H * s_count + ML_NEW_ROW_H;   // bottom of the "+ New alarm" row
  GRect area = GRect(8, free_top, b.size.w - 16, b.size.h - free_top);
  if (area.size.h <= 20) { return; }
  GSize sz = graphics_text_layout_get_content_size(
    msg, f, GRect(0, 0, area.size.w, 200), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  int w = sz.w;
  if (w > area.size.w) { w = area.size.w; }
  int x = area.origin.x + (area.size.w - w) / 2;
  // GOTHIC reserves headroom above the caps, so a measured content box
  // still sits low when centered; lift it back to the optical middle (same
  // constant/rationale as timer's empty_hint_update_proc).
  const int rise = 4;
  int y = area.origin.y + (area.size.h - sz.h) / 2 - rise;
  if (y < area.origin.y) { y = area.origin.y; }
  GRect hint = GRect(x, y, w, sz.h);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, msg, f, hint, GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

static void main_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);
  GRect menu_bounds = bounds;
  menu_bounds.size.h = bottom_bar_top_for_bounds(bounds);
  s_menu = menu_layer_create(menu_bounds);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = ml_num_rows, .get_cell_height = ml_cell_height,
    .draw_row = ml_draw_row, .select_click = ml_select,
    .select_long_click = ml_select_long });
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));

  s_main_hint_layer = layer_create(menu_bounds);
  layer_set_update_proc(s_main_hint_layer, main_hint_update_proc);
  layer_add_child(root, s_main_hint_layer);

  s_bottom_bar_layer = bottom_bar_attach(root);
}

static void main_window_unload(Window *w) {
  menu_layer_destroy(s_menu); s_menu = NULL;
  layer_destroy(s_main_hint_layer); s_main_hint_layer = NULL;
  layer_destroy(s_bottom_bar_layer); s_bottom_bar_layer = NULL;
}

// Ticks every bottom bar's clock (and "Next" text, since crossing a minute
// boundary can change which alarm is soonest) once a minute, but only for
// whichever of these windows is currently the one on screen.
static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  Window *top = window_stack_get_top_window();
  if (s_bottom_bar_layer && top == s_window) { layer_mark_dirty(s_bottom_bar_layer); }
  if (s_time_bottom_bar && top == s_time_window) { layer_mark_dirty(s_time_bottom_bar); }
  if (s_cron_bottom_bar && top == s_cron_window) { layer_mark_dirty(s_cron_bottom_bar); }
  if (s_edit_bottom_bar && top == s_edit_window) { layer_mark_dirty(s_edit_bottom_bar); }
}

// ================================= AppMessage (phone settings) =================================

#ifdef APP_TEST_HOOKS
// Test-only channel: lets an automated harness (`pebble send-app-message`)
// create/mutate an alarm's full state in one round trip, including state no
// UI path can produce at all (already-fired-today, mid-snooze, a latched
// ring pending) — see CLAUDE.md's "Test hooks" section for key IDs and
// example invocations. Compiled out unless built with `APP_TEST_HOOKS=1
// pebble build` (see wscript) — a shipped build never exposes this to a
// paired phone.
static bool handle_test_message(DictionaryIterator *iter) {
  bool changed = false;
  Tuple *t;

  if ((t = dict_find(iter, MESSAGE_KEY_TestClearAlarms)) && t->value->int32 != 0) {
    s_count = 0;
    changed = true;
  }

  if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmIndex))) {
    int idx = (int)t->value->int32;
    Alarm *a = NULL;
    if (idx == s_count && s_count < MAX_ALARMS) {
      // Same baseline defaults as start_new_alarm_flow()'s draft, except the
      // time: fixed at 7:00 here (rather than "next minute") so a test
      // harness gets a deterministic default unless it overrides hour/minute.
      a = &s_alarms[s_count];
      memset(a, 0, sizeof(*a));
      a->id = next_alarm_id();
      a->last_fired_day = -1;
      a->enabled = true;
      a->vibration_enabled = true;
      a->vibe_pattern = 0;
      a->sound_enabled = true;
      a->snooze_minutes = (uint16_t)s_default_snooze_minutes;
      a->snooze_max = (uint8_t)s_default_snooze_max;
      a->hour = 7; a->minute = 0;
      s_count++;
      changed = true;
    } else if (idx >= 0 && idx < s_count) {
      a = &s_alarms[idx];
    } else {
      APP_LOG(APP_LOG_LEVEL_WARNING, "TestAlarmIndex %d out of range (s_count=%d)", idx, s_count);
    }

    if (a) {
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmHour))) { a->hour = (uint8_t)t->value->int32; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmMinute))) { a->minute = (uint8_t)t->value->int32; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmRepeats))) { a->repeats = t->value->int32 != 0; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmRepeatDays))) { a->repeat_days = (uint8_t)t->value->int32; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmEnabled))) { a->enabled = t->value->int32 != 0; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmSkipNext))) { a->skip_next = t->value->int32 != 0; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmSnoozeMinutes))) { a->snooze_minutes = (uint16_t)t->value->int32; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmSnoozeMax))) { a->snooze_max = (uint8_t)t->value->int32; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmVibrationEnabled))) { a->vibration_enabled = t->value->int32 != 0; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmVibePattern))) { a->vibe_pattern = (uint8_t)t->value->int32; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmSoundEnabled))) { a->sound_enabled = t->value->int32 != 0; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmIncreasingVolume))) { a->increasing_volume = t->value->int32 != 0; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmName))) {
        strncpy(a->name, t->value->cstring, NAME_LEN);
        a->name[NAME_LEN] = '\0';
        changed = true;
      }
      // Seconds from now until the snooze deadline (0 = clear/not snoozed) —
      // an offset, not an absolute epoch, so a test harness doesn't need to
      // know the device's current clock.
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmSnoozeInSec))) {
        int32_t sec = t->value->int32;
        a->snooze_until = (sec > 0) ? (now_s() + sec) : 0;
        changed = true;
      }
      // Opaque day-id stamping (see ac_is_due/ac_mark_fired) — exposed as a
      // bool since a test harness has no way to construct the real day id.
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmLastFiredToday))) {
        a->last_fired_day = (t->value->int32 != 0) ? now_day_id() : -1;
        changed = true;
      }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmPending))) { a->alarm_pending = t->value->int32 != 0; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmIsCron))) { a->is_cron = t->value->int32 != 0; changed = true; }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmCronMinute))) {
        strncpy(a->cron_min, t->value->cstring, CRON_FIELD_LEN - 1);
        a->cron_min[CRON_FIELD_LEN - 1] = '\0';
        if (!ac_cron_parse_field(a->cron_min, 0, 59, &a->cron_min_mask)) {
          APP_LOG(APP_LOG_LEVEL_WARNING, "invalid TestAlarmCronMinute %s", a->cron_min);
        }
        changed = true;
      }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmCronHour))) {
        strncpy(a->cron_hour, t->value->cstring, CRON_FIELD_LEN - 1);
        a->cron_hour[CRON_FIELD_LEN - 1] = '\0';
        uint64_t hour_mask64 = 0;
        if (!ac_cron_parse_field(a->cron_hour, 0, 23, &hour_mask64)) {
          APP_LOG(APP_LOG_LEVEL_WARNING, "invalid TestAlarmCronHour %s", a->cron_hour);
        }
        a->cron_hour_mask = (uint32_t)hour_mask64;
        changed = true;
      }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmCronDow))) {
        strncpy(a->cron_dow, t->value->cstring, CRON_FIELD_LEN - 1);
        a->cron_dow[CRON_FIELD_LEN - 1] = '\0';
        uint64_t dow_mask = 0;
        if (!ac_cron_parse_field(a->cron_dow, 0, 6, &dow_mask)) {
          APP_LOG(APP_LOG_LEVEL_WARNING, "invalid TestAlarmCronDow %s", a->cron_dow);
        }
        a->repeat_days = (uint8_t)dow_mask;
        changed = true;
      }
      // Minute-granularity sibling of TestAlarmLastFiredToday, for cron's
      // exact-epoch-minute dedup (ac_cron_is_due) rather than day dedup.
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmCronFiredNow))) {
        a->cron_last_fired_min = (t->value->int32 != 0) ? (int32_t)(now_s() / 60) : -1;
        changed = true;
      }
      if ((t = dict_find(iter, MESSAGE_KEY_TestAlarmAutoStop))) { a->auto_stop = t->value->int32 != 0; changed = true; }
    }
  }

  return changed;
}
#endif // APP_TEST_HOOKS

static void inbox_received(DictionaryIterator *iter, void *ctx) {
  bool changed = false;
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_FirstDayOfWeek))) { s_first_day_of_week = (int)t->value->int32; changed = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_AlarmVibePattern))) { s_default_vibe_pattern = (int)t->value->int32; changed = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_DefaultSnoozeMinutes))) { s_default_snooze_minutes = (int)t->value->int32; changed = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_DefaultSnoozeMax))) { s_default_snooze_max = (int)t->value->int32; changed = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_AudioVolume))) { s_audio_volume = (int)t->value->int32; changed = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_DefaultSoundEnabled))) { s_default_sound_enabled = t->value->int32 != 0; changed = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_DefaultVibrationEnabled))) { s_default_vibration_enabled = t->value->int32 != 0; changed = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_DefaultIncreasingVolume))) { s_default_increasing_volume = t->value->int32 != 0; changed = true; }
  if (changed) { persist_all(); reload_ui(); }
#ifdef APP_TEST_HOOKS
  if (handle_test_message(iter)) { persist_all(); rearm_wakeup(); reload_ui(); }
#endif
}

static void outbox_sent(DictionaryIterator *iter, void *ctx) {}
static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox send failed: %d", (int)reason);
}

// ================================= init / deinit =================================

// wakeup_get_launch_event() (in init()) only covers a wakeup that had to
// relaunch the app because it wasn't running. If the app is already open
// (foreground) when the armed wakeup fires, the OS instead delivers it here
// — without this subscription there was no handler at all for that case, so
// a wakeup firing while the app happened to be open silently did nothing:
// no ring, no state change, until the app was later closed and reopened.
static void handle_wakeup_event(WakeupId id, int32_t cookie) {
  bool fired = sweep_due_alarms();
  if (fired) { persist_all(); }
  rearm_wakeup();
  reload_ui();
  if (fired) { show_next_pending_alarm(); }
}

static void init(void) {
  s_count = store_load(s_alarms);   // snooze_until/snooze_count load with the alarm, since they're persisted now
  s_first_day_of_week = store_load_first_day_of_week();
  s_default_vibe_pattern = store_load_vibe_pattern();
  s_default_snooze_minutes = store_load_default_snooze_minutes();
  s_default_snooze_max = store_load_default_snooze_max();
  s_next_local_id = store_load_next_local_id();
  s_audio_volume = store_load_audio_volume();
  s_default_vibration_enabled = store_load_default_vibration_enabled();
  s_default_sound_enabled = store_load_default_sound_enabled();
  s_default_increasing_volume = store_load_default_increasing_volume();

  // If launched by a wakeup, the firing event was already consumed.
  WakeupId wid; int32_t cookie;
  s_launched_by_wakeup = wakeup_get_launch_event(&wid, &cookie);
  if (s_launched_by_wakeup) { store_save_wakeup_id(-1); }
  bool fired = sweep_due_alarms();
  if (fired) { persist_all(); }
  rearm_wakeup();
  wakeup_service_subscribe(handle_wakeup_event);
  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);

  app_message_register_inbox_received(inbox_received);
  app_message_register_outbox_sent(outbox_sent);
  app_message_register_outbox_failed(outbox_failed);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = main_window_load, .unload = main_window_unload });
  window_stack_push(s_window, true);
  rebuild_order();

  if (fired) { show_next_pending_alarm(); }
}

static void deinit(void) {
  persist_all();
  rearm_wakeup();
  if (s_confirm_window) { window_destroy(s_confirm_window); }
  if (s_time_window) { window_destroy(s_time_window); }
  if (s_repeat_window) { window_destroy(s_repeat_window); }
  if (s_snooze_window) { window_destroy(s_snooze_window); }
  if (s_edit_window) { window_destroy(s_edit_window); }
  if (s_alarm_window) { window_destroy(s_alarm_window); }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
