/* PULSER: drives the bus like a device waiting for a response (keep busy
   bits: PULSE_US high every PERIOD_US). Otherwise: calls can_start() 1000
   times and prints how many times it returned true (bus wrongly free). */
#ifndef SWBB_INITIAL_DELAY
  #define SWBB_INITIAL_DELAY 1
#endif
#include <PJONSoftwareBitBang.h>
#ifndef PULSE_US
  #define PULSE_US 9.5
#endif
#ifndef PERIOD_US
  #define PERIOD_US 52
#endif
PJONSoftwareBitBang bus(1);
static void print_number(uint32_t n) {
  char b[11]; uint8_t i = 10; b[10] = 0;
  do { b[--i] = '0' + (n % 10); n /= 10; } while(n);
  Serial.print(b + i);
}
void setup() {
  Serial.begin(115200);
#ifdef PULSER
  cli();
  DDRD |= 4;
  for(;;) {
    PORTD |= 4;
    __builtin_avr_delay_cycles((uint32_t)(PULSE_US * 16));
    PORTD &= ~4;
    __builtin_avr_delay_cycles((uint32_t)((PERIOD_US - PULSE_US) * 16) - 5);
  }
#endif
  bus.strategy.set_pin(2);
  bus.begin();
}
void loop() {
  uint16_t free_count = 0;
  for(uint16_t i = 0; i < 1000; i++) {
    if(bus.strategy.can_start()) free_count++;
    delayMicroseconds(2 + (i % 37)); // vary the phase
  }
  Serial.print("DONE can_start() true "); print_number(free_count);
  Serial.println(" of 1000 while keep busy bits are transmitted");
  for(;;);
}
