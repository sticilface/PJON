/* Counts how many times bus.receive() can be called in one second when the
   bus is idle: a measure of the CPU time the strategy takes when polling */
#ifndef SWBB_INITIAL_DELAY
  #define SWBB_INITIAL_DELAY 1
#endif
#include <PJONSoftwareBitBang.h>
PJONSoftwareBitBang bus(1);
static void print_number(uint32_t n) {
  char b[11]; uint8_t i = 10; b[10] = 0;
  do { b[--i] = '0' + (n % 10); n /= 10; } while(n);
  Serial.print(b + i);
}
static void receiver(uint8_t *, uint16_t, const PJON_Packet_Info &) {}
void setup() {
  Serial.begin(115200);
  bus.strategy.set_pin(2);
  bus.set_receiver(receiver);
  bus.begin();
}
void loop() {
  uint32_t n = 0, start = micros();
  while(micros() - start < 1000000UL) { bus.receive(); n++; }
  Serial.print("DONE receive() calls per second: "); print_number(n);
  uint32_t t = micros();
  for(uint8_t i = 0; i < 100; i++) bus.strategy.can_start();
  Serial.print(" can_start() us: "); print_number((micros() - t) / 100);
  Serial.println("");
  for(;;);
}
