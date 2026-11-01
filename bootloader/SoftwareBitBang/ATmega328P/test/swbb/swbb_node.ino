/* SoftwareBitBang link test node (see run_swbb_tests.sh)
   ROLE_TX: sends COUNT packets of LENGTH bytes to PEER, one attempt each
   (no retries) so the result is the raw link reliability, then prints
   "DONE ok fail time_us". Otherwise receives and prints "DONE received bad"
   once COUNT packets (or more) were received or after RX_TIMEOUT ms. */

#ifndef SWBB_INITIAL_DELAY
  #define SWBB_INITIAL_DELAY 1
#endif
#include <PJONSoftwareBitBang.h>

#ifndef BUS_PIN
  #define BUS_PIN 2
#endif
#ifndef COUNT
  #define COUNT 200
#endif
#ifndef LENGTH
  #define LENGTH 20
#endif
#ifndef RX_TIMEOUT
  #define RX_TIMEOUT 4000
#endif

PJONSoftwareBitBang bus(MY_ID);
static uint16_t received = 0, bad = 0;
static uint8_t seen[32];
static uint32_t work = 0; // BUSY_US units of application work done

static void print_number(uint32_t n) {
  char b[11];
  uint8_t i = 10;
  b[10] = 0;
  do { b[--i] = '0' + (n % 10); n /= 10; } while(n);
  Serial.print(b + i);
}

static void receiver(uint8_t *payload, uint16_t length,
  const PJON_Packet_Info &info) {
  (void)info;
  received++;
  if(length != LENGTH) { bad++; return; }
  seen[(payload[0] >> 3) & 31] |= 1 << (payload[0] & 7);
  for(uint16_t i = 1; i < length; i++)
    if(payload[i] != (uint8_t)(payload[0] + i * 7)) { bad++; return; }
}

#ifdef LOAD_ISR
/* Interrupt load: Timer2 interrupt every 256us busy for LOAD_ISR us, like a
   serial or I2C interrupt handler */
ISR(TIMER2_COMPA_vect) {
  __builtin_avr_delay_cycles((uint32_t)LOAD_ISR * 16);
}
#endif

void setup() {
  Serial.begin(115200);
#ifdef LOAD_ISR
  TCCR2A = (1 << WGM21);           // CTC
  TCCR2B = (1 << CS22) | (1 << CS21); // F_CPU / 256: 16us per tick
  OCR2A = 15;                       // every 256us
  TIMSK2 = (1 << OCIE2A);
#endif
#ifdef SWBB_PIN
  bus.strategy.set_pin(SWBB_PIN);
#else
  bus.strategy.set_pin(BUS_PIN);
#endif
  bus.set_receiver(receiver);
  bus.begin();
}

void loop() {
#ifdef ROLE_TX
  static uint16_t n = 0, ok = 0, fail = 0;
  static uint32_t start, elapsed;
  if(!n) start = micros();
  static uint8_t payload[LENGTH];
  if(n == 0 && millis() < 10) return; // let the receiver start
  if(n < COUNT) {
    for(uint16_t i = 0; i < LENGTH; i++) payload[i] = (uint8_t)(n + i * 7);
    #ifdef FOREIGN
      /* Before each packet a packet to device 77 (not present): its frame
         and the keep busy bits that follow are traffic to ignore */
      bus.send_packet(77, payload, LENGTH);
    #endif
    if(bus.send_packet(PEER, payload, LENGTH) == PJON_ACK) ok++;
    else fail++;
    n++;
    #ifdef TX_GAP_MS
      delay(TX_GAP_MS); // Time between packets
    #endif
    if(n == COUNT) {
      elapsed = micros() - start;
      Serial.print("DONE ok "); print_number(ok);
      Serial.print(" fail "); print_number(fail);
      Serial.print(" us "); print_number(elapsed);
      Serial.println("");
    }
  }
  bus.receive(200);
#else
  #ifdef BUSY_US
    /* The application is busy for BUSY_US between two bus.receive() calls */
    bus.receive();
    work++;
    for(uint32_t w = BUSY_US; w; ) {
      uint16_t d = (w > 10000) ? 10000 : w;
      delayMicroseconds(d);
      w -= d;
    }
  #else
    bus.receive(1000);
  #endif
  static bool printed = false;
  if(!printed && ((received >= COUNT) || (millis() > RX_TIMEOUT))) {
    printed = true;
    Serial.print("DONE received "); print_number(received);
    Serial.print(" bad "); print_number(bad);
    #ifdef SWBB_INTERRUPT
      Serial.print(" frames "); print_number(swbb_irq::frames);
      Serial.print(" ignored "); print_number(swbb_irq::ignored);
      Serial.print(" missing:");
      uint8_t shown = 0;
      for(uint16_t i = 0; i < COUNT && i < 256 && shown < 8; i++)
        if(!(seen[i >> 3] & (1 << (i & 7)))) {
          Serial.print(" "); print_number(i); shown++;
        }
    #endif
    #ifdef BUSY_US
      Serial.print(" work "); print_number(work);
      Serial.print(" ms "); print_number(millis());
    #endif
    #ifdef SWBB_INTERRUPT_WAKE
      Serial.print(" wakes "); print_number(swbb_wake::wakes);
      Serial.print(" stored "); print_number(swbb_wake::stored);
      Serial.print(" foreign "); print_number(swbb_wake::foreign);
      Serial.print(" dropped "); print_number(swbb_wake::dropped);
      Serial.print(" failed "); print_number(swbb_wake::failed_first);
      Serial.print("/"); print_number(swbb_wake::failed_init);
      Serial.print("/"); print_number(swbb_wake::failed_byte);
    #endif
    Serial.println("");
  }
#endif
}
