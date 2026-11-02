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
   discarded, so the PC can always resynchronize.

   Packets received on the bootloader port (announcements of devices waiting
   in the bootloader) are forwarded to the PC at any time, also between a
   request and its answer:
     Uploader -> PC: '#' [sender id] [length] [length bytes of payload]
   The Uploader uses id PJON_BOOT_MASTER_ID (254), the id bootloaders
   announce themselves to.

   The bootloader acknowledges every packet quickly (pages are confirmed by
   a ping sent after them), so default PJON settings work: with the default
   chunk size of 32 bytes the longest packet is 48 bytes (49 with the 24 bits
   address of an ATmega2560 target). Only a chunk size
   of 64 or 128 (PJON_BOOT_CHUNK_SIZE, must match the bootloader) needs a
   longer packet buffer, set below. Optionally define PJON_INCLUDE_PACKET_ID
   to send packet ids, the bootloader accepts packets with or without. */

#include <utils/bootloader/PJON_Bootloader.h>

#if PJON_BOOT_CHUNK_SIZE > 32 && !defined(PJON_PACKET_MAX_LENGTH)
  #define PJON_PACKET_MAX_LENGTH \
    (PJON_BOOT_MAX_OVERHEAD + PJON_BOOT_WRITE_FAR_LENGTH + 1)
#endif
// Packets are sent with send_packet_blocking, no buffered packets needed
#define PJON_MAX_PACKETS 1
#define PJON_INCLUDE_PORT

#include <PJONSoftwareBitBang.h>

#ifndef BUS_PIN
  #define BUS_PIN 12
#endif
#define SERIAL_TIMEOUT_MS 250

PJONSoftwareBitBang bus(PJON_BOOT_MASTER_ID);

// The longest payload is a far write ('X', 24 bits address, ATmega2560)
uint8_t request[2 + PJON_BOOT_WRITE_FAR_LENGTH];
uint16_t received = 0;
uint32_t last_byte_time = 0;
uint16_t packet_id = 1;

/* Forwards packets on the bootloader port to the PC */
void receiver_function(
  uint8_t *payload,
  uint16_t length,
  const PJON_Packet_Info &info
) {
  if((info.port != PJON_BOOT_PORT) || (length > 255)) return;
  Serial.write('#');
  Serial.write(info.tx.id);
  Serial.write((uint8_t)length);
  for(uint16_t i = 0; i < length; i++) Serial.write(payload[i]);
};

void setup() {
  bus.strategy.set_pin(BUS_PIN);
  bus.set_crc_32(true); // The bootloader accepts only CRC32
  #if PJON_INCLUDE_PACKET_ID
    bus.set_packet_id(true);
  #endif
  bus.set_receiver(receiver_function);
  bus.begin();
  Serial.begin(115200);
};

void loop() {
  bus.receive(1000);
  while(Serial.available()) {
    if(received && ((uint32_t)(millis() - last_byte_time) > SERIAL_TIMEOUT_MS))
      received = 0;
    last_byte_time = millis();
    request[received++] = Serial.read();
    if(received == 2 && request[1] > PJON_BOOT_WRITE_FAR_LENGTH) {
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
        packet_id++,
        PJON_BOOT_PORT
      );
      if(!packet_id) packet_id = 1;
      Serial.write((result == PJON_ACK) ? 'K' : 'E');
    }
  }
};
