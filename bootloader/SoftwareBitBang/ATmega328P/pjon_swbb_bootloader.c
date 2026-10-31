/* PJON SoftwareBitBang bootloader for ATmega328P
   ___________________________________________________________________________

   Minimal bootloader receiving a firmware image over a single wire PJON bus
   using the SoftwareBitBang strategy (PJDL v5.0). It is written in plain C
   without the PJON library or the Arduino core so that it fits in 1kB.

   Supported (everything else is not needed to flash and is dropped):
   - PJDL frame reception and synchronous acknowledgement, one mode chosen
     at build time (MODE=1..4, default 1)
   - PJON local mode packets addressed to the bootloader's device id
   - CRC32 packet verification (covers meta-data, the CRC8 is not checked)
   - Optional sender info and packet id (skipped), port PJON_BOOT_PORT
   - Commands: ping, write page (erase + write + verify), start application

   Not supported: shared mode, MAC, extended length, broadcast, routing,
   transmitting packets and medium access (it never starts a transmission,
   it only answers with an acknowledgement), receiving while a page is being
   written (the uploader waits for the acknowledgement anyway).

   Entering the bootloader (the BOOTRST fuse makes it run on every reset):
   - Watchdog reset with a valid mailbox set by the application (see
     src/utils/bootloader/PJON_Bootloader.h): active for TIMEOUT_MS,
     answering to the device id passed by the application.
   - External reset (reset pin), if EXTRF_TIMEOUT_MS is not 0: active for
     EXTRF_TIMEOUT_MS answering to DEVICE_ID. Useful to recover a board
     whose application does not include the hook.
   - No application in flash (erased reset vector): active until a valid
     application is written and started.
   - Any other reset (power on, brown out, plain watchdog reset): the
     application is started immediately.

   The timeout is restarted by every valid command. The value of MCUSR at
   reset is passed to the application in GPIOR0 (MCUSR is cleared here).
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

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <stdint.h>

#include "utils/bootloader/PJON_Bootloader.h"

/* Build time configuration (see Makefile) --------------------------------- */

#ifndef PJON_PIN          // Arduino pin number of the bus (0-19, A0 = 14)
  #define PJON_PIN       12
#endif
#ifndef DEVICE_ID         // Id used if the application does not pass one
  #define DEVICE_ID      44
#endif
#ifndef MODE              // PJDL mode 1, 2, 3 or 4
  #define MODE            1
#endif
#ifndef TIMEOUT_MS        // Stay in the bootloader after an app request
  #define TIMEOUT_MS   5000
#endif
#ifndef EXTRF_TIMEOUT_MS  // Stay in the bootloader after the reset pin, 0=off
  #define EXTRF_TIMEOUT_MS 1000
#endif
#ifndef BOOT_START        // Byte address of the bootloader section
  #define BOOT_START 0x7C00
#endif

/* Pin mapping (Arduino numbering, ATmega328P) ----------------------------- */

#if PJON_PIN >= 0 && PJON_PIN <= 7
  #define BUS_PORT PORTD
  #define BUS_DDR  DDRD
  #define BUS_IN   PIND
  #define BUS_BIT  (PJON_PIN)
#elif PJON_PIN >= 8 && PJON_PIN <= 13
  #define BUS_PORT PORTB
  #define BUS_DDR  DDRB
  #define BUS_IN   PINB
  #define BUS_BIT  (PJON_PIN - 8)
#elif PJON_PIN >= 14 && PJON_PIN <= 19
  #define BUS_PORT PORTC
  #define BUS_DDR  DDRC
  #define BUS_IN   PINC
  #define BUS_BIT  (PJON_PIN - 14)
#else
  #error "PJON_PIN must be an Arduino pin number between 0 and 19"
#endif

#define BUS_HIGH()     (BUS_IN & (1 << BUS_BIT))
#define BUS_DRIVE()    (BUS_DDR |= (1 << BUS_BIT))
#define BUS_SET()      (BUS_PORT |= (1 << BUS_BIT))
#define BUS_CLEAR()    (BUS_PORT &= ~(1 << BUS_BIT))
/* Input without pull-up, the bus pull-down resistor keeps it low */
#define BUS_RELEASE()  do { BUS_DDR &= ~(1 << BUS_BIT); BUS_CLEAR(); } while(0)

/* PJDL v5.0 timing, as transmitted on the wire (microseconds) ------------- */

#if MODE == 1
  #define PAD_US 110
  #define BIT_US  44
  #define ACC_US  56
#elif MODE == 2
  #define PAD_US  92
  #define BIT_US  40
  #define ACC_US  56
#elif MODE == 3
  #define PAD_US  70
  #define BIT_US  28
  #define ACC_US  30
#elif MODE == 4
  #define PAD_US  65
  #define BIT_US  26
  #define ACC_US  30
#else
  #error "MODE must be 1, 2, 3 or 4"
#endif

/* Longest acceptable padding bit (PJDL tolerance is +17us) */
#define PAD_MAX_US (PAD_US + 20)

/* Timer1 runs at F_CPU / 8 and is used to measure durations */
#define TICKS(us) ((uint16_t)(((uint32_t)(us) * (F_CPU / 1000000UL)) / 8))
#define CYCLES(us) ((uint32_t)(us) * (F_CPU / 1000000UL))
#define CYCLES_BIT CYCLES(BIT_US)
#define CYCLES_PAD CYCLES(PAD_US)

/* Number of timer overflows (every 524288 cycles) for a timeout */
#define OVERFLOWS(ms) \
  ((uint16_t)(((uint64_t)(ms) * (F_CPU / 1000UL)) / 524288UL + 1))

/* PJON definitions ------------------------------------------------------- */

#define PJON_ACK            6
#define PJON_MODE_BIT       0x01
#define PJON_TX_INFO_BIT    0x02
#define PJON_ACK_REQ_BIT    0x04
#define PJON_MAC_BIT        0x08
#define PJON_PORT_BIT       0x10
#define PJON_CRC_BIT        0x20
#define PJON_EXT_LEN_BIT    0x40
#define PJON_PACKET_ID_BIT  0x80

/* id + header + length + CRC8 + tx id + packet id + port + payload + CRC32 */
#define BUFFER_LENGTH (4 + 1 + 2 + 2 + PJON_BOOT_WRITE_LENGTH + 4)
#define MIN_LENGTH    (4 + 2 + 1 + 4)

#if BUFFER_LENGTH > 255
  #error "Packet does not fit in 8 bits length"
#endif

/* Not initialized: there is no C runtime startup code (-nostartfiles) */
static uint8_t buffer[BUFFER_LENGTH] __attribute__((section(".noinit")));
static uint8_t device_id __attribute__((section(".noinit")));

/* SoftwareBitBang reception ----------------------------------------------- */

/* Synchronization pad: a high padding bit followed by a low data bit.
   Returns 1 at the end of the low bit if the pattern is recognised.
   If the bus is still low (the previous bit was a 0 slightly longer than
   expected) the rising edge is awaited for up to one bit. */
static uint8_t sync(void) {
  uint16_t start = TCNT1, elapsed;
  BUS_RELEASE();
  while(!BUS_HIGH())
    if((uint16_t)(TCNT1 - start) > TICKS(BIT_US)) return 0;
  start = TCNT1;
  do elapsed = TCNT1 - start;
  while(BUS_HIGH() && (elapsed <= TICKS(PAD_MAX_US)));
  if(elapsed < TICKS(ACC_US)) return 0;
  __builtin_avr_delay_cycles(CYCLES_BIT / 2);
  if(BUS_HIGH()) return 0;
  __builtin_avr_delay_cycles(CYCLES_BIT / 2);
  return 1;
}

/* Reads 8 bits LSB first sampling in the middle of each bit */
static uint8_t read_byte(void) {
  uint8_t value = 0, i = 8;
  __builtin_avr_delay_cycles(CYCLES_BIT / 2);
  for(;;) {
    value >>= 1;
    if(BUS_HIGH()) value |= 0x80;
    if(!--i) break;
    __builtin_avr_delay_cycles(CYCLES_BIT - 7); // minus loop overhead
  }
  __builtin_avr_delay_cycles(CYCLES_BIT / 2);
  return value;
}

/* Receives a frame (3 sync pads then sync pad + byte for each byte).
   Called when the bus is high. Returns the packet length or 0. */
static uint8_t receive_frame(void) {
  uint8_t length = BUFFER_LENGTH, i;
  if(!sync() || !sync() || !sync()) return 0;
  for(i = 0; i < length; i++) {
    if(!sync()) return 0;
    uint8_t b = read_byte();
    buffer[i] = b;
    if(i == 0) {
      if(b != device_id) return 0;
    } else if(i == 1) {
      if(b & (PJON_MODE_BIT | PJON_MAC_BIT | PJON_EXT_LEN_BIT)) return 0;
      if((b & (PJON_CRC_BIT | PJON_PORT_BIT)) != (PJON_CRC_BIT | PJON_PORT_BIT))
        return 0;
    } else if(i == 2) {
      if((b > BUFFER_LENGTH) || (b < MIN_LENGTH)) return 0;
      length = b;
    }
  }
  return length;
}

/* SoftwareBitBang synchronous acknowledgement ----------------------------- */

static void send_pad(void) {
  BUS_SET();
  BUS_DRIVE(); // Set high before driving, never pull against a high pulse
  __builtin_avr_delay_cycles(CYCLES_PAD);
  BUS_CLEAR();
  __builtin_avr_delay_cycles(CYCLES_BIT);
}

/* The transmitter keeps the bus busy with short high pulses while waiting:
   synchronize to the falling edge of one of them and answer. After the
   falling edge PJON keeps driving the pin low for a few microseconds before
   switching it to input (digitalWrite then pinMode), so wait a bit before
   driving the bus high. The pulse period depends on PJON's execution time
   more than on the mode (~55us on a 16MHz ATmega328P). */
#define RESPONSE_DELAY_US 18
#define PULSE_WAIT_US    100
static void send_ack(void) {
  uint16_t start = TCNT1;
  uint8_t value = PJON_ACK, i;
  while(!BUS_HIGH() && ((uint16_t)(TCNT1 - start) < TICKS(PULSE_WAIT_US)));
  start = TCNT1;
  while(BUS_HIGH() && ((uint16_t)(TCNT1 - start) < TICKS(BIT_US / 2)));
  __builtin_avr_delay_cycles(CYCLES(RESPONSE_DELAY_US));
  send_pad();
  send_pad();
  for(i = 8; i; i--) {
    if(value & 1) BUS_SET();
    else BUS_CLEAR();
    value >>= 1;
    __builtin_avr_delay_cycles(CYCLES_BIT - 8); // minus loop overhead
  }
  BUS_RELEASE();
}

/* Error detection --------------------------------------------------------- */

static uint32_t crc32(const uint8_t *data, uint8_t length) {
  uint32_t crc = 0xFFFFFFFF;
  while(length--) {
    crc ^= *data++;
    for(uint8_t i = 8; i; i--)
      crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : (crc >> 1);
  }
  return ~crc;
}

/* Flash ------------------------------------------------------------------- */

static uint8_t page_matches(uint16_t address, const uint8_t *data) {
  for(uint8_t i = 0; i < SPM_PAGESIZE; i++)
    if(pgm_read_byte(address + i) != data[i]) return 0;
  return 1;
}

/* Erases, writes and verifies a page. Returns 1 if the page content in flash
   is correct. The bootloader section can not be overwritten. */
static uint8_t write_page(uint16_t address, const uint8_t *data) {
  if((address & (SPM_PAGESIZE - 1)) || (address >= BOOT_START)) return 0;
  if(page_matches(address, data)) return 1;
  boot_page_erase(address);
  boot_spm_busy_wait();
  for(uint8_t i = 0; i < SPM_PAGESIZE; i += 2)
    boot_page_fill(address + i, data[i] | (data[i + 1] << 8));
  boot_page_write(address);
  boot_spm_busy_wait();
  boot_rww_enable();
  return page_matches(address, data);
}

/* Application ------------------------------------------------------------- */

/* Starts the application if present, otherwise returns */
static void start_app(void) {
  if(pgm_read_word(0) == 0xFFFF) return;
  TCCR1B = 0;
  TCNT1 = 0;
  TIFR1 = 0xFF;
  BUS_RELEASE();
  __asm__ __volatile__ ("jmp 0");
}

/* Main -------------------------------------------------------------------- */

int main(void) __attribute__((OS_main, section(".init9")));

int main(void) {
  __asm__ __volatile__ ("clr __zero_reg__");
  /* Read and clear the mailbox before anything is pushed on the stack */
  uint8_t magic_0 = PJON_BOOT_MAILBOX[0];
  uint8_t magic_1 = PJON_BOOT_MAILBOX[1];
  uint8_t id = PJON_BOOT_MAILBOX[2];
  PJON_BOOT_MAILBOX[0] = 0;
  uint8_t reset_flags = MCUSR;
  MCUSR = 0;
  wdt_disable();
  GPIOR0 = reset_flags;

  uint16_t timeout = 0; // 0 = wait forever (only if there is no app)
  device_id = DEVICE_ID;
  if(
    (reset_flags & (1 << WDRF)) &&
    (magic_0 == PJON_BOOT_MAGIC_0) && (magic_1 == PJON_BOOT_MAGIC_1)
  ) {
    if(id && (id < 254)) device_id = id;
    timeout = OVERFLOWS(TIMEOUT_MS);
  }
#if EXTRF_TIMEOUT_MS
  else if(reset_flags & (1 << EXTRF)) timeout = OVERFLOWS(EXTRF_TIMEOUT_MS);
#endif
  else start_app(); // Returns only if there is no application

  BUS_RELEASE();
  TCCR1A = 0;
  TCCR1B = (1 << CS11); // F_CPU / 8
  uint16_t remaining = timeout;

  for(;;) {
    if(TIFR1 & (1 << TOV1)) {
      TIFR1 = (1 << TOV1);
      if(timeout && !--remaining) {
        start_app();
        remaining = timeout; // No application, keep waiting
      }
    }
    if(!BUS_HIGH()) continue;
    uint8_t length = receive_frame();
    if(!length) continue;
    /* The meta-data CRC8 is not checked: the CRC32 covers the whole packet
       (meta-data and CRC8 included) and the length is already bounded */
    uint32_t crc = crc32(buffer, length - 4);
    uint8_t i = length;
    while((i > length - 4) && (buffer[--i] == (uint8_t)crc)) crc >>= 8;
    if(i > length - 4) continue; // CRC32 is transmitted MSB first
    uint8_t header = buffer[1], index = 4;
    if(header & PJON_TX_INFO_BIT) index++;
    if(header & PJON_PACKET_ID_BIT) index += 2;
    if(
      (buffer[index] != (uint8_t)(PJON_BOOT_PORT >> 8)) ||
      (buffer[index + 1] != (uint8_t)PJON_BOOT_PORT)
    ) continue;
    index += 2;
    uint8_t *payload = buffer + index;
    uint8_t payload_length = (length - 4) - index;
    uint8_t ok = 0;
    if(payload_length == 1)
      ok = (payload[0] == PJON_BOOT_CMD_PING) ||
           (payload[0] == PJON_BOOT_CMD_GO);
    else if(
      (payload_length == PJON_BOOT_WRITE_LENGTH) &&
      (payload[0] == PJON_BOOT_CMD_WRITE)
    ) ok = write_page((payload[1] << 8) | payload[2], payload + 3);
    if(!ok) continue; // No acknowledgement, the uploader will retry
    if(header & PJON_ACK_REQ_BIT) send_ack();
    remaining = timeout;
    if(payload[0] == PJON_BOOT_CMD_GO) start_app();
  }
}
