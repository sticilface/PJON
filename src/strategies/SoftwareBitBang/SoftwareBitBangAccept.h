/* SoftwareBitBang: checks done before an acknowledgement sent by an interrupt
   ___________________________________________________________________________

   The interrupt driven receivers (SWBB_INTERRUPT_WAKE, mode 1-7) acknowledge
   a frame before PJON reads it. They must acknowledge only frames that
   PJON::receive() (PJON.h) would acknowledge itself, otherwise the
   transmitter is told the packet was delivered and PJON drops it (seen on
   hardware: a receiver built without PJON_INCLUDE_PACKET_ID acknowledged
   frames carrying a packet id).

   swbb_accept() repeats the checks PJON::receive() does before it calls
   strategy.send_response(), for a device in local mode, not in router mode
   and not PJON_SIMPLEX (the interrupt receivers do not support these):
   - addressed to this device id (broadcast frames are never acknowledged)
   - extended length requires CRC32, packet id requires PJON_INCLUDE_PACKET_ID,
     port requires PJON_INCLUDE_PORT, MAC requires PJON_INCLUDE_MAC and CRC32
   - length >= overhead + 1 and < PJON_PACKET_MAX_LENGTH, CRC32 above 15 bytes
   - CRC8 of the header and CRC8 or CRC32 of the frame (verified by the caller)
   As in PJON, a frame repeating a known packet id is acknowledged (and then
   dropped by PJON as a duplicate), and a frame for another port is
   acknowledged (PJON filters the port after the acknowledgement).

   Returns:
   SWBB_REJECT     PJON drops it: do not acknowledge, do not store
   SWBB_ACCEPT     PJON accepts it (acknowledge it if requested)
   SWBB_UNDECIDED  depends on run time state the strategy does not know
                   (PJON's MAC address or shared mode configuration): store
                   it without acknowledging, PJON decides */

#pragma once

#define SWBB_REJECT    0
#define SWBB_ACCEPT    1
#define SWBB_UNDECIDED 2

static uint8_t swbb_accept(const uint8_t *d, uint16_t length) {
  uint8_t h = d[1];
  bool ext = h & PJON_EXT_LEN_BIT, mac = h & PJON_MAC_BIT;
  if((d[0] == PJON_BROADCAST) && (h & PJON_ACK_REQ_BIT)) return SWBB_REJECT;
  if(ext && !(h & PJON_CRC_BIT)) return SWBB_REJECT;
  if(!PJON_INCLUDE_PACKET_ID && (h & PJON_PACKET_ID_BIT)) return SWBB_REJECT;
  if(!PJON_INCLUDE_PORT && (h & PJON_PORT_BIT)) return SWBB_REJECT;
  if((!PJON_INCLUDE_MAC && mac) || (mac && !(h & PJON_CRC_BIT)))
    return SWBB_REJECT;
  if(
    (length < (uint8_t)(PJONTools::packet_overhead(h) + 1)) ||
    (length >= PJON_PACKET_MAX_LENGTH)
  ) return SWBB_REJECT;
  if((length > 15) && !(h & PJON_CRC_BIT)) return SWBB_REJECT;
  // A device in local mode accepts shared mode frames only with bus id 0
  if(h & PJON_MODE_BIT) {
    for(uint8_t i = 4 + ext; i < 8 + ext; i++)
      if(d[i]) return SWBB_REJECT;
    return SWBB_UNDECIDED; // Unless PJON is configured in shared mode
  }
  if(mac) return SWBB_UNDECIDED; // PJON compares the MAC address
  return SWBB_ACCEPT;
}
