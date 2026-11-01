/* Test application that never calls pjon_boot_app_ok(): with the boot guard
   the watchdog resets the device after BOOT_GUARD_S seconds and the
   bootloader must stay active and announce the failure. Linked with
   app_marker.cpp (APP_MARKER=3) and the Arduino core replacement. */

#include "Arduino.h"

void setup() {}
void loop() {}
