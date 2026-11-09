// Reads lines "S|<text>" (sound/tune pattern) or "V|<text>" (vibration pattern)
// from stdin and prints 1 if the watch's real parser (alarm_calc.c) accepts
// it, else 0. Used by tests/pattern_parity.test.js to check that the phone's
// TypeScript validators (list_config.ts and the copies inside the config
// components) accept exactly what the watch does.
#include <stdio.h>
#include <string.h>
#include "alarm_calc.h"

int main(void) {
  char line[512];
  while (fgets(line, sizeof(line), stdin)) {
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) { line[--n] = '\0'; }
    if (n < 2 || line[1] != '|') { printf("?\n"); continue; }
    const char *text = line + 2;
    uint8_t flags[64], notes[64], len;
    int ok = (line[0] == 'S')
        ? ac_sound_pattern_parse(text, AC_SOUND_PATTERN_MAX_SEGMENTS, flags, notes, &len)
        : ac_vibe_pattern_parse(text, AC_VIBE_PATTERN_MAX_SEGMENTS, flags, &len);
    printf("%d\n", ok ? 1 : 0);
  }
  return 0;
}
