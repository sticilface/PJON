/* PJON SoftwareBitBang bootloader - application with bootloader hook

   Example application for a device flashed with the PJON SoftwareBitBang
   bootloader (bootloader/SoftwareBitBang/ATmega328P). It blinks the LED and
   listens on the bus: when it receives the bootloader request sent by
   tools/pjon_boot_upload.py it restarts into the bootloader, which then
   answers to the same device id until the new firmware is written or the
   bootloader timeout expires.

   Requirements for any application that wants to be updated over PJON:
   - define PJON_INCLUDE_PORT before including PJON
   - call bus.receive() regularly
   - in the receiver function call pjon_boot_enter() when
     pjon_boot_is_request() is true */

#define PJON_INCLUDE_PORT

#include <utils/bootloader/PJON_Bootloader.h>
#include <PJONSoftwareBitBang.h>

#ifndef BUS_PIN
  #define BUS_PIN 12
#endif

PJONSoftwareBitBang bus(44);

void receiver_function(
  uint8_t *payload,
  uint16_t length,
  const PJON_Packet_Info &info
) {
  if(pjon_boot_is_request(payload, length, info.port))
    pjon_boot_enter(bus.device_id()); // Does not return
  // Handle the application's own packets here
};

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  bus.strategy.set_pin(BUS_PIN);
  bus.set_receiver(receiver_function);
  bus.begin();
};

void loop() {
  static uint32_t last_toggle = 0;
  if((uint32_t)(millis() - last_toggle) > 500) {
    last_toggle = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
  bus.receive(1000);
};
