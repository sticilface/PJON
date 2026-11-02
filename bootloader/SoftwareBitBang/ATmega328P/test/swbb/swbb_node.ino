/* SoftwareBitBang link test node (see run_swbb_tests.sh)
   ROLE_TX: sends COUNT packets of LENGTH bytes to PEER, one attempt each
   (no retries) so the result is the raw link reliability, then prints
   "DONE ok fail time_us". Otherwise receives and prints "DONE received bad"
   once COUNT packets (or more) were received or after RX_TIMEOUT ms. */

#ifndef SWBB_INITIAL_DELAY
  #define SWBB_INITIAL_DELAY 1
#endif
#define SWBB_M7_STATS // Mode 7 statistics, printed with DONE
#define SWBB_WAKE_STATS // Wake receiver statistics, printed with DONE
#include <PJONSoftwareBitBang.h>
#include <utils/PJON_StackCheck.h>

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
static uint16_t from[8];   // packets received per sender id (& 7)
#ifndef RX_COUNT           // packets the receiver waits for (all senders)
  #define RX_COUNT COUNT
#endif

static void print_number(uint32_t n) {
  char b[11];
  uint8_t i = 10;
  b[10] = 0;
  do { b[--i] = '0' + (n % 10); n /= 10; } while(n);
  Serial.print(b + i);
}

static void receiver(uint8_t *payload, uint16_t length,
  const PJON_Packet_Info &info) {
  received++;
  from[info.tx.id & 7]++;
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
  pjon_stack_paint();
  Serial.begin(115200);
#ifdef SAME_SEED // All devices the same random sequence: worst case
  randomSeed(1);
#else
  randomSeed(MY_ID * 7919UL + 1); // Different random sequences per device
#endif
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
#ifdef TX_PACKET_ID // The packets carry a packet id (needs PJON_INCLUDE_PACKET_ID)
  bus.set_packet_id(true);
#endif
#ifdef TX_PORT      // The packets carry a port (needs PJON_INCLUDE_PORT)
  bus.include_port(TX_PORT);
#endif
#ifdef TX_MAC       // The packets carry MAC addresses
  bus.include_mac(true);
#endif
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
    #ifndef PAYLOAD_OFFSET
      #define PAYLOAD_OFFSET 0
    #endif
    for(uint16_t i = 0; i < LENGTH; i++)
      payload[i] = (uint8_t)(n + PAYLOAD_OFFSET + i * 7);
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
      #if SWBB_MODE == 7
        Serial.print(" window_ticks "); print_number(swbb_m7::max_ticks);
        Serial.print(" overruns "); print_number(swbb_m7::overruns);
        Serial.print(" gave_up "); print_number(swbb_m7::collisions);
        #ifdef SWBB_M7_WINDOW_STATS
          Serial.print(" ticks");
          for(uint8_t i = 0; i < 16; i++) {
            Serial.print(" "); print_number(swbb_m7::window_hist[i]);
          }
        #endif
      #endif
      Serial.println("");
    }
  }
  #ifndef TX_RECEIVE_US // Receive time between packets (0: back to back)
    #define TX_RECEIVE_US 200
  #endif
  bus.receive(TX_RECEIVE_US);
#else
  #ifdef RX_STOP_MS
    /* After RX_STOP_MS the application is stuck: bus.receive() is no longer
       called (the interrupt receiver must stop acknowledging) */
    static bool stuck = false;
    if(millis() > RX_STOP_MS) stuck = true;
    if(stuck) {
      #ifdef RX_STUCK_EMPTY
        /* The queue looks empty although nobody reads it (as after a stack
           overflow): only SWBB_WAKE_ALIVE_MS can stop the acknowledgements */
        #if SWBB_MODE == 7
          swbb_m7::count = 0;
        #else
          swbb_wake::count = 0;
        #endif
      #endif
      static bool printed_stuck = false;
      if(!printed_stuck && (millis() > RX_TIMEOUT)) {
        printed_stuck = true;
        Serial.print("DONE received "); print_number(received);
        Serial.println(" stuck");
      }
      return;
    }
  #endif
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
  if(!printed && ((received >= RX_COUNT) || (millis() > RX_TIMEOUT))) {
    printed = true;
    Serial.print("DONE received "); print_number(received);
    Serial.print(" bad "); print_number(bad);
    Serial.print(" stack_free "); print_number(pjon_stack_free());
    for(uint8_t i = 0; i < 8; i++)
      if(from[i]) {
        Serial.print(" from"); print_number(i);
        Serial.print(" "); print_number(from[i]);
      }
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
    #if SWBB_MODE == 7
      Serial.print(" m7fail "); print_number(swbb_m7::fail_long);
      Serial.print("/"); print_number(swbb_m7::fail_short);
      Serial.print("/"); print_number(swbb_m7::fail_start);
      Serial.print("/"); print_number(swbb_m7::fail_stop);
      Serial.print("/"); print_number(swbb_m7::fail_late);
    #endif
    #if defined(SWBB_INTERRUPT_WAKE) && (SWBB_MODE == 7)
      Serial.print(" wakes "); print_number(swbb_m7::wakes);
      Serial.print(" stored "); print_number(swbb_m7::stored);
      Serial.print(" dropped "); print_number(swbb_m7::dropped);
      Serial.print(" failed "); print_number(swbb_m7::failed);
    #elif defined(SWBB_INTERRUPT_WAKE)
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
