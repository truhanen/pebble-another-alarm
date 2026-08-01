#!/usr/bin/env bash
# Captures reference screenshots of this app's key screens using the Pebble
# CLI emulator, driven entirely by APP_TEST_HOOKS (src/c/main.c's
# handle_test_message()) rather than by manually clicking/typing through the
# on-watch UI - see CLAUDE.md's "Test hooks (APP_TEST_HOOKS)" section. This
# means the whole thing is fully scriptable, no manual/touch steps needed
# (unlike pebble-another-timer's create_screenshots.sh, which has to pause
# for a touch dial and a keyboard - this app has neither).
#
# NOTE: this kills ALL currently running Pebble emulators (any platform) and
# wipes their on-watch storage, so it can start from a known-empty alarm
# list. Don't run it if you have unsaved state in another emulator.
#
# NOTE: builds with APP_TEST_HOOKS=1, which compiles in the test-message
# handler. Never ship/install a non-test build made this way to a real watch.
#
# Per CLAUDE.md's "Emulator testing (headless)" section, every command that
# touches the emulator passes --vnc, so screenshots work in a headless
# environment without pebble-tool silently restarting qemu underneath us.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="emery"
OUT_DIR="$REPO_ROOT/screenshots"
SKIP_BUILD=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [-o OUT_DIR] [--skip-build]

  -o, --output     Directory to write screenshots into. Default: $OUT_DIR
      --skip-build Skip the APP_TEST_HOOKS build and reuse the existing
                   build/ output (it must already have been built with
                   APP_TEST_HOOKS=1, or the test-message keys won't exist).
  -h, --help       Show this help.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    -o|--output) OUT_DIR="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

cd "$REPO_ROOT"
mkdir -p "$OUT_DIR"

PBW="build/pebble-another-alarm.pbw"

log() { printf '\n=== %s ===\n' "$1"; }

shoot() {
  # $1: output filename (without directory)
  pebble screenshot --vnc --no-open "$OUT_DIR/$1"
  echo "Saved $OUT_DIR/$1"
  sleep 1
}

button() {
  # $1: back|up|select|down   $2: number of presses (default 1)
  # $3: seconds to sleep after each press (default 1)
  local n="${2:-1}" delay="${3:-1}"
  local i
  for ((i = 0; i < n; i++)); do
    pebble emu-button --vnc click "$1"
    sleep "$delay"
  done
}

long_press_select() {
  # $1: seconds to sleep afterwards (default 1). Mirrors `make
  # long_press_emulator BUTTON=select`. Only the MAIN LIST's row long-press
  # (ml_select_long, opening the edit menu) is wired up this way - the edit
  # menu's own MenuLayer has no select_long_click handler at all, so a long
  # press on one of ITS rows just behaves like a normal short click.
  pebble emu-button --vnc --duration 700 click select
  sleep "${1:-1}"
}

if [ "$SKIP_BUILD" -eq 0 ]; then
  log "Building app (APP_TEST_HOOKS=1)"
  APP_TEST_HOOKS=1 pebble build || APP_TEST_HOOKS=1 pebble build
fi

if [ ! -f "$PBW" ]; then
  echo "Could not find $PBW - build failed?" >&2
  exit 1
fi

# Read numeric AppMessage keys from the just-built output instead of
# hardcoding them: they're assigned by build order from package.json's
# messageKeys list (10000 + declaration index, see CLAUDE.md's AppMessage
# keys note) and could change if that list is edited.
message_key() {
  # $1: key name, e.g. TestClearAlarms. Lines look like
  # "uint32_t MESSAGE_KEY_TestClearAlarms = 10007;" - grab the value after
  # '=', not just any digit run (that would also match the "32" in
  # "uint32_t").
  sed -n "s/^uint32_t MESSAGE_KEY_$1 = \([0-9]*\);/\1/p" build/src/message_keys.auto.c
}
CLEAR_KEY="$(message_key TestClearAlarms)"
INDEX_KEY="$(message_key TestAlarmIndex)"
HOUR_KEY="$(message_key TestAlarmHour)"
MINUTE_KEY="$(message_key TestAlarmMinute)"
REPEATS_KEY="$(message_key TestAlarmRepeats)"
REPEAT_DAYS_KEY="$(message_key TestAlarmRepeatDays)"
ENABLED_KEY="$(message_key TestAlarmEnabled)"
SKIP_NEXT_KEY="$(message_key TestAlarmSkipNext)"
NAME_KEY="$(message_key TestAlarmName)"
IS_CRON_KEY="$(message_key TestAlarmIsCron)"
CRON_MINUTE_KEY="$(message_key TestAlarmCronMinute)"
CRON_HOUR_KEY="$(message_key TestAlarmCronHour)"
CRON_DOW_KEY="$(message_key TestAlarmCronDow)"
CRON_DOM_KEY="$(message_key TestAlarmCronDom)"
CRON_MONTH_KEY="$(message_key TestAlarmCronMonth)"
CRON_WEEK_KEY="$(message_key TestAlarmCronWeek)"
for v in CLEAR_KEY INDEX_KEY HOUR_KEY MINUTE_KEY REPEATS_KEY REPEAT_DAYS_KEY ENABLED_KEY \
         SKIP_NEXT_KEY NAME_KEY IS_CRON_KEY CRON_MINUTE_KEY CRON_HOUR_KEY CRON_DOW_KEY \
         CRON_DOM_KEY CRON_MONTH_KEY CRON_WEEK_KEY; do
  if [ -z "${!v}" ]; then
    echo "Could not resolve message key $v from build/src/message_keys.auto.c - was this built with APP_TEST_HOOKS=1?" >&2
    exit 1
  fi
done

install_fresh() {
  # Kill any running emulators and wipe their storage first: `pebble wipe`
  # only deletes the on-disk persist directory, which a live qemu process
  # won't reload on its own, so the kill is required for a truly empty list.
  log "Resetting the emulator (kill + wipe) and installing"
  pebble kill
  pebble wipe
  pebble install --emulator "$PLATFORM" --vnc "$PBW"
  sleep 3
}

# ---------------------------------------------------------------------------
# Set up three named alarms via APP_TEST_HOOKS instead of the "+ New alarm"
# wizard - one weekday-repeating morning alarm, one cron pomodoro alarm, and
# one plain one-time alarm, so the main view screenshot shows a realistic,
# varied list. AC_DAY_MON|TUE|WED|THU|FRI = (1<<1)|(1<<2)|(1<<3)|(1<<4)|(1<<5)
# = 62 (see alarm_calc.h's AC_DAY_* bits).
# ---------------------------------------------------------------------------
install_fresh

log "Creating alarm 0: 'Wakeup' (07:00, repeats Mon-Fri, skip-next)"
pebble send-app-message --vnc \
  --int "${CLEAR_KEY}=1" "${INDEX_KEY}=0" "${HOUR_KEY}=7" "${MINUTE_KEY}=0" \
        "${REPEATS_KEY}=1" "${REPEAT_DAYS_KEY}=62" "${SKIP_NEXT_KEY}=1" \
  --string "${NAME_KEY}=Wakeup"
sleep 1

log "Creating alarm 1: 'Pomodoro' (cron: */25 9-17 * * 1-5 *, disabled)"
pebble send-app-message --vnc \
  --int "${INDEX_KEY}=1" "${IS_CRON_KEY}=1" "${ENABLED_KEY}=0" \
  --string "${NAME_KEY}=Pomodoro" "${CRON_MINUTE_KEY}=*/25" "${CRON_HOUR_KEY}=9-17" \
           "${CRON_DOW_KEY}=1-5" "${CRON_DOM_KEY}=*" "${CRON_MONTH_KEY}=*" "${CRON_WEEK_KEY}=*"
sleep 1

log "Creating alarm 2: 'Movie' (20:00, one-time)"
pebble send-app-message --vnc \
  --int "${INDEX_KEY}=2" "${HOUR_KEY}=20" "${MINUTE_KEY}=0" "${REPEATS_KEY}=0" "${REPEAT_DAYS_KEY}=0" \
  --string "${NAME_KEY}=Movie"
sleep 1

# ---------------------------------------------------------------------------
# Main list starts with the MenuLayer's default selection (row 0). Rows sort
# by raw clock time ascending: Wakeup (07:00), Pomodoro (cron sort key
# 9:00, its earliest possible fire), Movie (20:00). Wakeup's skip-next and
# Pomodoro's disabled state are both set directly via APP_TEST_HOOKS
# (TestAlarmSkipNext/TestAlarmEnabled) rather than by driving the main
# list's short-press state-cycle button-by-button - fewer button presses
# means fewer chances for the emulator's button/screenshot pipeline to
# drift, and it leaves the cursor on row 0 (Wakeup) for free, with no
# navigation needed before the first screenshot.
# ---------------------------------------------------------------------------
log "Main view (3 alarms, Wakeup skip-next + focused, Pomodoro disabled)"
shoot "01_main_view.png"

# ---------------------------------------------------------------------------
# Time + Repeat screenshots, via 'Wakeup' (row 0, non-cron)'s edit menu.
# Row order (edit_build_rows, non-cron): Delete, Time, Label, State, Repeat,
# Snooze, Vibration, Vibe pattern, Sound, Increasing volume, Auto-stop.
# ---------------------------------------------------------------------------
log "Opening 'Wakeup' edit menu (long-press select)"
long_press_select 1   # ml_select_long -> opens the edit menu directly, Delete (row 0) selected

log "Moving to the Time row"
button down 1   # Delete (0) -> Time (1)

log "Time configuration screen"
button select 1 1   # short-press: opens the time editor directly (hour box focused)
shoot "02_time_config.png"

log "Returning to edit menu"
button back 1 1   # BACK on the first field (hour) cancels/reverts, popping back to the edit menu

log "Moving to the Repeat row"
button down 3 1   # Time (1) -> Label (2) -> State (3) -> Repeat (4)

log "Weekday repeat configuration screen"
button select 1 1   # short-press: opens the weekday repeat editor directly (Apply row selected)
shoot "03_repeat_config.png"

log "Returning to edit menu"
button back 1 1   # BACK cancels, popping back to the edit menu

log "Closing edit menu, back to main list"
button back 1 1   # cursor stays on the 'Wakeup' row

# ---------------------------------------------------------------------------
# Cron screenshot, via 'Pomodoro' (row 1, is_cron)'s own edit menu. Row order
# for a cron alarm collapses Time+Repeat into one Cron row: Delete, Cron,
# Label, State, Snooze, Vibration, Vibe pattern, Sound, Increasing volume,
# Auto-stop.
# ---------------------------------------------------------------------------
log "Moving to the 'Pomodoro' row"
button down 1 1   # Wakeup (0) -> Pomodoro (1)

log "Opening 'Pomodoro' edit menu (long-press select)"
long_press_select 1

log "Moving to the Cron row"
button down 1 1   # Delete (0) -> Cron (1)

log "Cron configuration screen"
button select 1 1   # short-press: opens the cron editor directly (Apply row selected)
shoot "04_cron_config.png"

log "Returning to edit menu"
button back 1 1   # BACK cancels, popping back to the edit menu

log "Closing edit menu, back to main list"
button back 1 1

# ---------------------------------------------------------------------------
# One-alarm main view, to show the one-alarm hint overlay
# (main_hint_update_proc draws "- Short-press to cycle state\n- Long-press to
# edit" in the blank space below the list, but only when s_count == 1).
# Resetting via TestClearAlarms+TestAlarmIndex=0 is simpler than a full
# kill/wipe/install cycle - it wipes the 3 existing alarms and appends a
# fresh alarm, then reconfigured to be identical to the 'Wakeup' alarm from
# the first main view screenshot (07:00, repeats Mon-Fri), but left enabled
# (normal) rather than skip-next.
# ---------------------------------------------------------------------------
log "Resetting to a single 'Wakeup' alarm (07:00, repeats Mon-Fri, enabled) for the one-alarm hint view"
pebble send-app-message --vnc \
  --int "${CLEAR_KEY}=1" "${INDEX_KEY}=0" "${HOUR_KEY}=7" "${MINUTE_KEY}=0" \
        "${REPEATS_KEY}=1" "${REPEAT_DAYS_KEY}=62" \
  --string "${NAME_KEY}=Wakeup"
sleep 1

log "Main view (1 alarm, hint text shown, '+ New alarm' focused)"
shoot "05_main_view_hint.png"

log "Done - screenshots saved in $OUT_DIR"
