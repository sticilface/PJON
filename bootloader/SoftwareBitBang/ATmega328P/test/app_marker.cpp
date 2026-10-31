/* Linked into the example application by the simulation test: writes the
   application version in GPIOR1 at startup, so the simulator can tell which
   application is running. */

#include <avr/io.h>

#ifndef APP_MARKER
  #define APP_MARKER 1
#endif

__attribute__((constructor)) static void app_marker(void) {
  GPIOR1 = APP_MARKER;
}
