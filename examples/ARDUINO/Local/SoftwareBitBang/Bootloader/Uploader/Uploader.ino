/* PJON SoftwareBitBang bootloader - Uploader

   Serial to PJON bridge used by tools/pjon_boot_upload.py (see
   bootloader/SoftwareBitBang/ATmega328P/README.md) to flash a device
   running the PJON SoftwareBitBang bootloader.

   Wiring: connect pin 12 of this board to the bus pin of the target(s),
   together with GND. The bus needs a pull-down resistor (see the
   SoftwareBitBang README).

   Serial protocol (115200 baud), one request at a time:
     PC -> Uploader: [device id] [length] [length bytes of payload]
     Uploader -> PC: 'K' if the packet was acknowledged, 'E' if not
   A request with device id 0 and length 0 is answered with 'R' (ready).
   If the bytes of a request are more than 250ms apart the request is
   discarded, so the PC can always resynchronize. */

// Write packets are 142 bytes long, a longer buffer is required
#define PJON_PACKET_MAX_LENGTH PJON_BOOT_UPLOADER_PACKET_MAX_LENGTH
// The bootloader acknowledges after writing the page (~9ms)
#define SWBB_RESPONSE_OFFSET PJON_BOOT_UPLOADER_RESPONSE_OFFSET
// Packets are sent with send_packet_blocking, no buffered packets needed
#define PJON_MAX_PACKETS 1
#define PJON_INCLUDE_PORT

#include <utils/bootloader/PJON_Bootloader.h>
#include <PJONSoftwareBitBang.h>

#ifndef BUS_PIN
  #define BUS_PIN 12
#endif
#define SERIAL_TIMEOUT_MS 250

PJONSoftwareBitBang bus(PJON_MASTER_ID);

uint8_t request[2 + PJON_BOOT_WRITE_LENGTH];
uint16_t received = 0;
uint32_t last_byte_time = 0;

void setup() {
  bus.strategy.set_pin(BUS_PIN);
  bus.set_crc_32(true);         // The bootloader accepts only CRC32
  bus.include_sender_info(false);
  bus.begin();
  Serial.begin(115200);
};

void loop() {
  while(Serial.available()) {
    if(received && ((uint32_t)(millis() - last_byte_time) > SERIAL_TIMEOUT_MS))
      received = 0;
    last_byte_time = millis();
    request[received++] = Serial.read();
    if(received == 2 && request[1] > PJON_BOOT_WRITE_LENGTH) {
      received = 0;
      Serial.write('E');
      continue;
    }
    if(received >= 2 && received == (uint16_t)(request[1] + 2)) {
      received = 0;
      if(!request[0]) {
        Serial.write('R');
        continue;
      }
      uint16_t result = bus.send_packet_blocking(
        request[0],
        request + 2,
        request[1],
        PJON_NO_HEADER,
        0,
        PJON_BOOT_PORT
      );
      Serial.write((result == PJON_ACK) ? 'K' : 'E');
    }
  }
};
