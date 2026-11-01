/* PJON SoftwareBitBang bootloader - shared definitions and application hook
   ___________________________________________________________________________

   This header is shared by:
   - the bootloader itself (bootloader/SoftwareBitBang/ATmega328P)
   - the application running on the target (to jump into the bootloader)
   - the uploader (the device that sends the firmware over PJON)

   It is plain C so the bootloader can include it without the PJON library.

   Over-the-air protocol (PJON local mode, port PJON_BOOT_PORT). Every packet
   sent to the bootloader must use CRC32: PJON uses it automatically only
   above 15 bytes, so senders must force it (bus.set_crc_32(true)) at least
   for the short commands.

   Application (normal PJON sketch, PJON_INCLUDE_PORT must be defined):
     'B'                         -> application restarts into the bootloader

   Bootloader:
     'P'                         -> ping: acknowledged only if the last page
                                    was written and verified (or no page was
                                    written yet)
     'W' addrH addrL data[CHUNK] -> one chunk of a flash page. addr is the
                                    byte address of the chunk, aligned to
                                    PJON_BOOT_CHUNK_SIZE. Chunks of a page
                                    must be sent in order; a chunk can be
                                    repeated (lost acknowledgement). When the
                                    last chunk of a page is received it is
                                    acknowledged, then the page is erased,
                                    written and verified (~9ms, the bootloader
                                    does not listen meanwhile). A missing or
                                    out of order chunk makes the page fail.
     'G'                         -> start the application: acknowledged only
                                    if the last page is correct

   Writing a page: send its chunks, then a ping. The ping is not answered
   while the page is being written (PJON retries), and is answered only if
   the page is in flash and verified, otherwise the page must be resent.
   This keeps every acknowledgement fast, so senders can use the default
   SWBB_RESPONSE_OFFSET and PJON_PACKET_MAX_LENGTH (chunk size 32).

   How the application enters the bootloader:
   The application writes a 3 bytes "mailbox" (magic + its own device id) at
   the top of SRAM and triggers a watchdog reset. SRAM survives a watchdog
   reset, so the bootloader finds the mailbox, clears it, and stays active
   using the same device id the application was using. If no command is
   received for PJON_BOOT_TIMEOUT_MS (set when building the bootloader) the
   application is started again.
   ___________________________________________________________________________

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */

#pragma once

#include <stdint.h>

/* PJON port used by the bootloader protocol (0xB007) */
#ifndef PJON_BOOT_PORT
  #define PJON_BOOT_PORT         45063
#endif

/* Commands (first payload byte) */
#define PJON_BOOT_CMD_ENTER      'B'
#define PJON_BOOT_CMD_PING       'P'
#define PJON_BOOT_CMD_WRITE      'W'
#define PJON_BOOT_CMD_GO         'G'

/* Flash page size of the ATmega328P in bytes */
#define PJON_BOOT_PAGE_SIZE      128

/* Bytes of flash carried by each write command: 16, 32, 64 or 128.
   Bootloader, uploader and pjon_boot_upload.py must use the same value.
   With 32 a write packet is at most 48 bytes long (sender info, packet id,
   port and CRC32 included) so it fits the default PJON_PACKET_MAX_LENGTH. */
#ifndef PJON_BOOT_CHUNK_SIZE
  #define PJON_BOOT_CHUNK_SIZE   32
#endif

#if (PJON_BOOT_CHUNK_SIZE != 16) && (PJON_BOOT_CHUNK_SIZE != 32) && \
    (PJON_BOOT_CHUNK_SIZE != 64) && (PJON_BOOT_CHUNK_SIZE != 128)
  #error "PJON_BOOT_CHUNK_SIZE must be 16, 32, 64 or 128"
#endif

#define PJON_BOOT_CHUNKS_PER_PAGE (PJON_BOOT_PAGE_SIZE / PJON_BOOT_CHUNK_SIZE)

/* Length of a write command payload: command + 16 bits address + chunk */
#define PJON_BOOT_WRITE_LENGTH   (3 + PJON_BOOT_CHUNK_SIZE)

/* Worst case PJON overhead of a packet sent to the bootloader: id, header,
   length, CRC8, sender id, packet id (2), port (2), CRC32 (4) */
#define PJON_BOOT_MAX_OVERHEAD   13

/* Mailbox used to pass the request from application to bootloader */
#define PJON_BOOT_MAGIC_0        0xB0
#define PJON_BOOT_MAGIC_1        0x07
#define PJON_BOOT_MAILBOX_SIZE   3

#if defined(__AVR__)
  #include <avr/io.h>
  #include <avr/interrupt.h>
  #include <avr/wdt.h>

  /* The mailbox lives in the last bytes of SRAM */
  #define PJON_BOOT_MAILBOX \
    ((volatile uint8_t *)(RAMEND - (PJON_BOOT_MAILBOX_SIZE - 1)))

  /* Call from the application to restart into the bootloader.
     device_id is the id the bootloader will answer to (normally the id of
     the application). This function never returns. */
  static inline void pjon_boot_enter(uint8_t device_id)
    __attribute__((noreturn));

  static inline void pjon_boot_enter(uint8_t device_id) {
    cli();
    /* The top of SRAM is the bottom of the stack. Nothing is returned to
       from here on, so overwriting it is harmless. */
    PJON_BOOT_MAILBOX[0] = PJON_BOOT_MAGIC_0;
    PJON_BOOT_MAILBOX[1] = PJON_BOOT_MAGIC_1;
    PJON_BOOT_MAILBOX[2] = device_id;
    wdt_enable(WDTO_15MS);
    for(;;);
  };

  /* Convenience check to be used in the application's receiver function:

     void receiver_function(
       uint8_t *payload, uint16_t length, const PJON_Packet_Info &info
     ) {
       if(pjon_boot_is_request(payload, length, info.port))
         pjon_boot_enter(bus.device_id());
       ...
     }

     The application must be compiled with PJON_INCLUDE_PORT defined,
     otherwise PJON drops packets that contain a port. */
  static inline uint8_t pjon_boot_is_request(
    const uint8_t *payload,
    uint16_t length,
    uint16_t port
  ) {
    return (port == PJON_BOOT_PORT) && (length == 1) &&
      (payload[0] == PJON_BOOT_CMD_ENTER);
  };
#endif
