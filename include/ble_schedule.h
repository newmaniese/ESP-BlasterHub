#ifndef BLE_SCHEDULE_H
#define BLE_SCHEDULE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

inline bool calculateScheduleCountdown(
    bool armed, uint32_t delayMs, unsigned long heartbeatMs, const char* commandName,
    unsigned long currentMs, uint32_t* out_seconds_remaining, char* out_command_name, size_t name_max) {

  if (!out_seconds_remaining || !out_command_name || name_max == 0) {
    return false;
  }

  if (!armed || !commandName || commandName[0] == '\0') {
    return false;
  }

  unsigned long elapsed = currentMs - heartbeatMs;
  if (elapsed >= delayMs) {
    return false;  // already expired, about to fire
  }

  // Calculate remaining seconds, rounding up
  *out_seconds_remaining = (delayMs - (uint32_t)elapsed + 999) / 1000;

  strncpy(out_command_name, commandName, name_max - 1);
  out_command_name[name_max - 1] = '\0';

  return true;
}

#endif // BLE_SCHEDULE_H
