/* SoftwareBitBang mode 7 link test (two ATmega328P, see REVIEW-swbb-mode7.md)

   Flash one board with TRANSMITTER defined and the other without, both on
   the same bus pin. The transmitter sends packets of LENGTH bytes to the
   receiver with one attempt each (no retries), so the counts are the raw
   reliability of the link, and prints every 5 seconds:
     sent, acknowledged, failed, payload kB/s
   The receiver prints how many packets it received and how many had a wrong
   content. Compare "acknowledged" (transmitter) with "received" (receiver):
   a packet received but not acknowledged means the response was lost.

   Change MODE to 6 or 3 to compare with the PJDL modes on the same cable.
   For a busy bus, flash more transmitters, each with its own MY_ID
   (e.g. -DMY_ID=3, same PEER). Two transmitters with the same id send
   identical frames when their counters match: if they start together the
   receiver gets one frame and both transmitters see its acknowledgement
   (acknowledged > received, as measured with two id 1 transmitters). */

#ifndef MODE
  #define MODE 7
#endif
#define SWBB_MODE MODE
#define SWBB_PIN  2              // Bus pin (direct port access)
// #define SWBB_INTERRUPT_WAKE   // Receive in the INT0 interrupt (pin 2/3)
#define SWBB_INITIAL_DELAY 10    // Shorter start up delay (ms)
#define PJON_PACKET_MAX_LENGTH 80

#include <PJONSoftwareBitBang.h>

// #define TRANSMITTER
#ifndef LENGTH
  #define LENGTH 20
#endif

#ifdef TRANSMITTER
  #ifndef MY_ID
    #define MY_ID 1 // Unique for every transmitter
  #endif
  #define PEER  2
#else
  #define MY_ID 2
#endif

PJONSoftwareBitBang bus(MY_ID);
uint32_t received = 0, bad = 0;

void receiver_function(
  uint8_t *payload, uint16_t length, const PJON_Packet_Info &info
) {
  (void)info;
  received++;
  if(length != LENGTH) { bad++; return; }
  for(uint16_t i = 1; i < length; i++)
    if(payload[i] != (uint8_t)(payload[0] + i * 7)) { bad++; return; }
}

void setup() {
  Serial.begin(115200);
  bus.set_receiver(receiver_function);
  bus.begin();
  Serial.print("Mode ");
  Serial.print(MODE);
  #ifdef TRANSMITTER
    Serial.println(" transmitter");
  #else
    Serial.println(" receiver");
  #endif
}

void loop() {
  static uint32_t last = millis(), sent = 0, acked = 0, failed = 0;
#ifdef TRANSMITTER
  static uint8_t payload[LENGTH], n = 0;
  for(uint16_t i = 0; i < LENGTH; i++) payload[i] = (uint8_t)(n + i * 7);
  n++;
  sent++;
  if(bus.send_packet(PEER, payload, LENGTH) == PJON_ACK) acked++;
  else failed++;
  bus.receive(0);
#else
  bus.receive(1000);
#endif
  if((uint32_t)(millis() - last) >= 5000) {
    uint32_t elapsed = millis() - last;
    last = millis();
#ifdef TRANSMITTER
    Serial.print("sent "); Serial.print(sent);
    Serial.print(" acknowledged "); Serial.print(acked);
    Serial.print(" failed "); Serial.print(failed);
    Serial.print(" payload kB/s ");
    Serial.println((float)acked * LENGTH / elapsed, 2);
    sent = acked = failed = 0;
#else
    Serial.print("received "); Serial.print(received);
    Serial.print(" bad "); Serial.println(bad);
    received = bad = 0;
#endif
  }
}
