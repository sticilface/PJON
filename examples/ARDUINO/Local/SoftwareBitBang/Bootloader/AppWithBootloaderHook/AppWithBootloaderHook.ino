/* PJON SoftwareBitBang bootloader - application with bootloader hook

   Example application for a device flashed with the PJON SoftwareBitBang
   bootloader (bootloader/SoftwareBitBang/ATmega328P). It blinks the LED and
   listens on the bus:
   - 'B' on the bootloader port restarts it into the bootloader, which then
     answers to the same device id until the new firmware is written or the
     bootloader timeout expires
   - 'Y' (identify) on the bootloader port blinks the LED quickly for 3
     seconds, to find the device

   Requirements for any application that wants to be updated over PJON:
   - define PJON_INCLUDE_PORT before including PJON (and PJON_INCLUDE_PACKET_ID
     if the sender, e.g. a router, adds packet ids)
   - use the device id assigned by the bootloader: pjon_boot_saved_id()
   - call pjon_boot_app_ok() once the application works (end of setup()):
     with the boot guard the bootloader resets the device and waits for new
     firmware if it is not called within BOOT_GUARD_S seconds
   - call bus.receive() regularly
   - in the receiver function call pjon_boot_enter() when
     pjon_boot_is_request() is true */

#define PJON_INCLUDE_PORT

#include <utils/bootloader/PJON_Bootloader.h>
#include <PJONSoftwareBitBang.h>

#ifndef BUS_PIN
  #define BUS_PIN 12
#endif
// Id used if the device has not been provisioned
#define FALLBACK_ID 44

PJONSoftwareBitBang bus;
uint32_t identify_until = 0;

void receiver_function(
  uint8_t *payload,
  uint16_t length,
  const PJON_Packet_Info &info
) {
  if(pjon_boot_is_request(payload, length, info.port))
    pjon_boot_enter(bus.device_id()); // Does not return
  if(
    (info.port == PJON_BOOT_PORT) && (length == 1) &&
    (payload[0] == PJON_BOOT_CMD_IDENTIFY)
  ) identify_until = millis() + 3000;
  // Handle the application's own packets here
};

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  bus.set_id(pjon_boot_saved_id(FALLBACK_ID));
  bus.strategy.set_pin(BUS_PIN);
  bus.set_receiver(receiver_function);
  bus.begin();
  pjon_boot_app_ok(); // The application works: disable the boot guard
};

void loop() {
  static uint32_t last_toggle = 0;
  uint16_t period = ((int32_t)(identify_until - millis()) > 0) ? 50 : 500;
  if((uint32_t)(millis() - last_toggle) > period) {
    last_toggle = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
  bus.receive(1000);
};
