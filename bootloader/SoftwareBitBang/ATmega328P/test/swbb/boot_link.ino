/* Link test with the bootloader (see run_boot_link.sh): sends COUNT write
   chunks to the bootloader (device id 44, no application, so it stays in the
   bootloader), one attempt each, and prints "DONE ok fail time_us". After the
   last chunk of a page it waits for the page to be written. */
#ifndef SWBB_INITIAL_DELAY
  #define SWBB_INITIAL_DELAY 1
#endif
#define PJON_MAX_PACKETS 1
#define PJON_INCLUDE_PORT
#include <utils/bootloader/PJON_Bootloader.h>
#include <PJONSoftwareBitBang.h>
#ifndef BUS_PIN
  #define BUS_PIN 2
#endif
#ifndef COUNT
  #define COUNT 200
#endif
PJONSoftwareBitBang bus(PJON_MASTER_ID);
static void print_number(uint32_t n) {
  char b[11]; uint8_t i = 10; b[10] = 0;
  do { b[--i] = '0' + (n % 10); n /= 10; } while(n);
  Serial.print(b + i);
}
#ifdef LOAD_ISR
/* Interrupt load: Timer2 interrupt every 256us busy for LOAD_ISR us */
ISR(TIMER2_COMPA_vect) {
  __builtin_avr_delay_cycles((uint32_t)LOAD_ISR * 16);
}
#endif

void setup() {
  Serial.begin(115200);
#ifdef LOAD_ISR
  TCCR2A = (1 << WGM21);
  TCCR2B = (1 << CS22) | (1 << CS21);
  OCR2A = 15;
  TIMSK2 = (1 << OCIE2A);
#endif
  bus.strategy.set_pin(BUS_PIN);
  bus.set_crc_32(true); // the bootloader accepts only CRC32
  bus.begin();
  delay(20); // let the bootloader start
}
#ifdef ANNOUNCE
/* -DANNOUNCE=<ms>: only listen for ANNOUNCE ms and count the announcements
   of the bootloader (fresh device without application) received and
   acknowledged by PJON */
static uint16_t announcements = 0;
static void receiver(uint8_t *payload, uint16_t length,
  const PJON_Packet_Info &info) {
  if((info.port == PJON_BOOT_PORT) && (length == PJON_BOOT_ANNOUNCE_LENGTH)
    && (payload[0] == PJON_BOOT_CMD_ANNOUNCE)) announcements++;
}
void loop() {
  bus.set_receiver(receiver);
  uint32_t start = millis();
  while(millis() - start < ANNOUNCE) bus.receive(1000);
  Serial.print("DONE announcements "); print_number(announcements);
  Serial.println("");
  for(;;);
}
#else
void loop() {
  uint16_t ok = 0, fail = 0;
  uint8_t payload[PJON_BOOT_WRITE_LENGTH];
  uint32_t start = micros();
  for(uint16_t n = 0; n < COUNT; n++) {
    uint16_t address = 0x1000 + (n % 16) * PJON_BOOT_CHUNK_SIZE;
    payload[0] = PJON_BOOT_CMD_WRITE;
    payload[1] = address >> 8;
    payload[2] = address & 0xFF;
    for(uint8_t i = 0; i < PJON_BOOT_CHUNK_SIZE; i++)
      payload[3 + i] = (uint8_t)(n * 3 + i * 7);
    if(bus.send_packet(44, payload, PJON_BOOT_WRITE_LENGTH,
      PJON_NO_HEADER, 0, PJON_BOOT_PORT) == PJON_ACK) ok++;
    else fail++;
    if(((address + PJON_BOOT_CHUNK_SIZE) & (PJON_BOOT_PAGE_SIZE - 1)) == 0)
      delay(15); // page erase and write
    else delayMicroseconds(500);
  }
  Serial.print("DONE ok "); print_number(ok);
  Serial.print(" fail "); print_number(fail);
  Serial.print(" us "); print_number(micros() - start);
  Serial.println("");
  for(;;);
}
#endif
