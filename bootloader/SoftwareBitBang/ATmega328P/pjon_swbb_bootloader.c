/* PJON SoftwareBitBang bootloader for ATmega328P
   ___________________________________________________________________________

   Minimal bootloader receiving a firmware image over a single wire PJON bus
   using the SoftwareBitBang strategy (PJDL v5.0). It is written in plain C
   without the PJON library or the Arduino core so that it fits in 2kB.

   Supported (everything else is not needed to flash and is dropped):
   - PJDL frame reception and synchronous acknowledgement, one mode chosen
     at build time (MODE=1..4, default 1)
   - PJON local mode packets addressed to the bootloader's device id
   - CRC32 packet verification (covers meta-data, the CRC8 is not checked)
   - Optional sender info and packet id (skipped), port PJON_BOOT_PORT
   - Commands: ping, write a page chunk (the page is erased, written and
     verified after its last chunk), start application, assign device id
     (see src/utils/bootloader/PJON_Bootloader.h for the protocol)
   - Announcements to the master while waiting without timeout (fresh
     device, failed application start), with medium access check
   - Device id and a random serial (generated on the first start if not
     written at ISP time) kept in EEPROM
   - Boot guard: the first start after an update is supervised by the
     watchdog until the application calls pjon_boot_app_ok()

   Not supported: shared mode, MAC, extended length, broadcast, routing,
   receiving while a page is being written (the sender's ping is retried by
   PJON until the write is over).

   Device id: the id passed by the application (bootloader request), else
   the id saved in EEPROM, else DEVICE_ID (default 255, not assigned).

   Entering the bootloader (the BOOTRST fuse makes it run on every reset):
   - Watchdog reset with a valid mailbox set by the application (see
     src/utils/bootloader/PJON_Bootloader.h): active for TIMEOUT_MS.
   - Watchdog reset while the application is pending (updated and not
     confirmed with pjon_boot_app_ok() within BOOT_GUARD_S seconds): active
     until a working application is written and started, announcing the
     failure.
   - External reset (reset pin), if EXTRF_TIMEOUT_MS is not 0: active for
     EXTRF_TIMEOUT_MS. Useful to recover a board whose application does not
     include the hook.
   - No application in flash (erased reset vector): active until a valid
     application is written and started, announcing itself.
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
#include <avr/eeprom.h>
#include <stdint.h>

#include "utils/bootloader/PJON_Bootloader.h"

/* Build time configuration (see Makefile) --------------------------------- */

#ifndef PJON_PIN          // Arduino pin number of the bus (0-19, A0 = 14)
  #define PJON_PIN       12
#endif
#ifndef DEVICE_ID         // Id used if none is passed or saved in EEPROM
  #define DEVICE_ID      PJON_BOOT_UNASSIGNED
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
  #define BOOT_START 0x7800
#endif
#ifndef BOOT_GUARD_S      // Seconds the app has to call pjon_boot_app_ok()
  #define BOOT_GUARD_S    8  // 0 (off), 1, 2, 4 or 8
#endif

#if BOOT_GUARD_S == 1
  #define BOOT_GUARD_WDTO WDTO_1S
#elif BOOT_GUARD_S == 2
  #define BOOT_GUARD_WDTO WDTO_2S
#elif BOOT_GUARD_S == 4
  #define BOOT_GUARD_WDTO WDTO_4S
#elif BOOT_GUARD_S == 8
  #define BOOT_GUARD_WDTO WDTO_8S
#elif BOOT_GUARD_S != 0
  #error "BOOT_GUARD_S must be 0, 1, 2, 4 or 8"
#endif

/* Announcements: first one within ~1s, then every 5s doubling up to 60s,
   plus up to ~1s of random jitter. A valid command postpones the next one
   by ANNOUNCE_MAX_MS. */
#define ANNOUNCE_FIRST_MS  5000
#define ANNOUNCE_MAX_MS   60000

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
#define BUFFER_LENGTH (PJON_BOOT_MAX_OVERHEAD + PJON_BOOT_WRITE_LENGTH)
#define MIN_LENGTH    (4 + 2 + 1 + 4)

#if BUFFER_LENGTH > 255
  #error "Packet does not fit in 8 bits length"
#endif

#if SPM_PAGESIZE != PJON_BOOT_PAGE_SIZE
  #error "Unexpected flash page size"
#endif

/* Not initialized: there is no C runtime startup code (-nostartfiles),
   main() assigns the variables that need an initial value. */
#define NOINIT __attribute__((section(".noinit")))
static uint8_t buffer[BUFFER_LENGTH] NOINIT;  // received or sent packet
static uint8_t page[SPM_PAGESIZE] NOINIT;     // page being assembled
static uint16_t page_address NOINIT;          // its address, NO_PAGE if none
static uint8_t next_chunk NOINIT;             // chunks received in order
static uint8_t last_page_ok NOINIT;           // result of the last page write
static uint8_t device_id NOINIT;
static uint8_t serial[4] NOINIT;              // copy of the EEPROM serial
static uint16_t random_state NOINIT;          // random8() state
static uint8_t flashed NOINIT;                // a page was written

#define NO_PAGE 0xFFFF

/* Error detection --------------------------------------------------------- */

/* CRC32 (PJON, IEEE 802.3), bit by bit. It is updated while the frame is
   received, one byte at a time at the start of the next padding bit (~110
   cycles: ~7us at 16MHz, ~14us at 8MHz, shorter than any padding bit minus
   ACC_US), so the acknowledgement can be sent right after the frame, well
   within the default PJON response timeout. */
static uint32_t crc32_update(uint32_t crc, uint8_t b) {
  crc ^= b;
  for(uint8_t i = 8; i; i--)
    crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
  return crc;
}

/* Meta-data CRC8 of the packets sent by the bootloader (PJON CRC8 C2) */
static uint8_t crc8(const uint8_t *data, uint8_t length) {
  uint8_t crc = 0;
  while(length--) {
    uint8_t b = *data++;
    for(uint8_t i = 8; i; i--, b >>= 1) {
      uint8_t mix = (crc ^ b) & 1;
      crc >>= 1;
      if(mix) crc ^= 0x97;
    }
  }
  return crc;
}

/* 16 bits linear congruential generator, seeded with the serial: devices
   pick different delays even if they start at the same time. Only the
   high byte is used. */
static uint8_t random8(void) {
  random_state = random_state * 25173 + 13849;
  return random_state >> 8;
}

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
   Called when the bus is high. Returns the packet length or 0, and in
   *crc the CRC32 of the packet without its last 4 bytes (the CRC32). */
static uint8_t receive_frame(uint32_t *crc) {
  uint8_t length = BUFFER_LENGTH, i;
  uint32_t c = 0xFFFFFFFF;
  if(!sync() || !sync() || !sync()) return 0;
  for(i = 0; i < length; i++) {
    if(!sync()) return 0;
    uint8_t b = read_byte();
    buffer[i] = b;
    if(i < length - 4) c = crc32_update(c, b);
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
  *crc = ~c;
  return length;
}

/* SoftwareBitBang transmission ------------------------------------------- */

static void send_pad(void) {
  BUS_SET();
  BUS_DRIVE(); // Set high before driving, never pull against a high pulse
  __builtin_avr_delay_cycles(CYCLES_PAD);
  BUS_CLEAR();
  __builtin_avr_delay_cycles(CYCLES_BIT);
}

/* Synchronization pad then 8 bits LSB first */
static void send_byte(uint8_t value) {
  send_pad();
  for(uint8_t i = 8; i; i--) {
    if(value & 1) BUS_SET();
    else BUS_CLEAR();
    value >>= 1;
    __builtin_avr_delay_cycles(CYCLES_BIT - 8); // minus loop overhead
  }
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
  while(!BUS_HIGH() && ((uint16_t)(TCNT1 - start) < TICKS(PULSE_WAIT_US)));
  start = TCNT1;
  while(BUS_HIGH() && ((uint16_t)(TCNT1 - start) < TICKS(BIT_US / 2)));
  __builtin_avr_delay_cycles(CYCLES(RESPONSE_DELAY_US));
  send_pad();
  send_byte(PJON_ACK);
  BUS_RELEASE();
}

/* Waits for the synchronous acknowledgement of a frame just sent: keeps
   the bus busy with short pulses, the receiver answers after the falling
   edge of one of them with 2 synchronization pads and PJON_ACK. */
#define ACK_TIMEOUT_US 4000
static uint8_t receive_ack(void) {
  uint16_t start = TCNT1;
  while((uint16_t)(TCNT1 - start) < TICKS(ACK_TIMEOUT_US)) {
    BUS_SET();
    BUS_DRIVE();
    __builtin_avr_delay_cycles(CYCLES_BIT / 4);
    BUS_RELEASE();
    if(sync() && sync()) return read_byte() == PJON_ACK;
  }
  return 0;
}

/* Announces the bootloader to the master. Returns 0 if the bus is busy (try
   again soon), 1 if the announcement was not acknowledged, 2 if it was. */
static uint8_t announce(uint8_t reason) {
  /* Medium access: the bus must stay low for a padding bit, 10 data bits
     and a random time (a byte cannot be shorter) */
  uint16_t start = TCNT1;
  uint16_t wait = TICKS(PAD_US + 10 * BIT_US) + (random8() & 127);
  while((uint16_t)(TCNT1 - start) < wait) if(BUS_HIGH()) return 0;
  uint8_t *p = buffer;
  *p++ = PJON_BOOT_MASTER_ID;
  *p++ = PJON_TX_INFO_BIT | PJON_ACK_REQ_BIT | PJON_PORT_BIT | PJON_CRC_BIT;
  *p++ = 4 + 1 + 2 + PJON_BOOT_ANNOUNCE_LENGTH + 4;
  *p = crc8(buffer, 3);
  p++;
  *p++ = device_id;
  *p++ = (uint8_t)(PJON_BOOT_PORT >> 8);
  *p++ = (uint8_t)PJON_BOOT_PORT;
  *p++ = PJON_BOOT_CMD_ANNOUNCE;
  *p++ = reason;
  for(uint8_t i = 0; i < 4; i++) *p++ = serial[i];
  *p++ = PJON_BOOT_VERSION;
  *p++ = PJON_BOOT_CHUNK_SIZE;
  uint32_t crc = 0xFFFFFFFF;
  for(uint8_t *q = buffer; q < p; q++) crc = crc32_update(crc, *q);
  crc = ~crc;
  for(int8_t shift = 24; shift >= 0; shift -= 8) *p++ = crc >> shift;
  send_pad(); // frame initializer: 3 synchronization pads
  send_pad();
  send_pad();
  for(uint8_t *q = buffer; q < p; q++) send_byte(*q);
  BUS_RELEASE();
  return receive_ack() + 1;
}

/* EEPROM ------------------------------------------------------------------ */

/* Loads the serial from EEPROM. On the first start (erased EEPROM) a random
   serial is generated from the jitter between the watchdog oscillator and
   the CPU clock, and saved. The first byte of a serial is never 0xFF. */
#define EEPROM_SERIAL ((uint8_t *)PJON_BOOT_EEPROM_SERIAL_ADDR)
static void load_serial(void) {
  if(eeprom_read_byte(EEPROM_SERIAL) == 0xFF) {
    uint32_t mix = 0;
    TCCR1B = (1 << CS10); // F_CPU
    WDTCSR = (1 << WDCE) | (1 << WDE);
    WDTCSR = (1 << WDIE); // 16ms, interrupt mode (interrupts stay off)
    for(uint8_t n = 32; n; n--) {
      while(!(WDTCSR & (1 << WDIF)));
      WDTCSR = (1 << WDIF) | (1 << WDIE);
      mix = crc32_update(mix, TCNT1L);
    }
    WDTCSR = (1 << WDCE) | (1 << WDE);
    WDTCSR = 0;
    mix &= 0xFFFFFF7F;
    for(uint8_t i = 0; i < 4; i++, mix >>= 8)
      eeprom_update_byte(EEPROM_SERIAL + i, mix);
  }
  uint32_t mix = 0;
  for(uint8_t i = 0; i < 4; i++) {
    serial[i] = eeprom_read_byte(EEPROM_SERIAL + i);
    mix = crc32_update(mix, serial[i]);
  }
  random_state = mix; // different serials give different delays
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
  eeprom_busy_wait(); // SPM is not possible while the EEPROM is written
  boot_page_erase(address);
  boot_spm_busy_wait();
  for(uint8_t i = 0; i < SPM_PAGESIZE; i += 2)
    boot_page_fill(address + i, data[i] | (data[i + 1] << 8));
  boot_page_write(address);
  boot_spm_busy_wait();
  boot_rww_enable();
  return page_matches(address, data);
}

/* Stores a chunk in the page buffer. Returns 0 if the address is invalid
   (not acknowledged), 1 if the chunk is accepted, 2 if it is the last chunk
   of the page (acknowledge, then call finish_page). A chunk that is not the
   next one in order (or a repetition of the previous one) invalidates the
   page, that then fails in finish_page. */
static uint8_t store_chunk(uint16_t address, const uint8_t *data) {
  if((address & (PJON_BOOT_CHUNK_SIZE - 1)) || (address >= BOOT_START))
    return 0;
  uint16_t base = address & ~(SPM_PAGESIZE - 1);
  uint8_t offset = address & (SPM_PAGESIZE - 1);
  uint8_t index = offset / PJON_BOOT_CHUNK_SIZE;
  if(!index) { // first chunk, start the page
    page_address = base;
    next_chunk = 0;
  }
  if(base != page_address) page_address = NO_PAGE;
  else if(index == next_chunk) next_chunk++;
  else if(index + 1 != next_chunk) page_address = NO_PAGE;
  if(page_address != NO_PAGE)
    for(uint8_t i = 0; i < PJON_BOOT_CHUNK_SIZE; i++)
      page[offset + i] = data[i];
  return (index == PJON_BOOT_CHUNKS_PER_PAGE - 1) ? 2 : 1;
}

/* Writes the page if all its chunks were received in order */
static void finish_page(void) {
  last_page_ok =
    (page_address != NO_PAGE) &&
    (next_chunk == PJON_BOOT_CHUNKS_PER_PAGE) &&
    write_page(page_address, page);
  flashed |= last_page_ok;
}

/* Application ------------------------------------------------------------- */

static uint8_t app_present(void) {
  return pgm_read_word(0) != 0xFFFF;
}

#define EEPROM_FLAG ((uint8_t *)PJON_BOOT_EEPROM_FLAG_ADDR)

static uint8_t app_pending(void) {
  return eeprom_read_byte(EEPROM_FLAG) == PJON_BOOT_FLAG_PENDING;
}

/* Starts the application if present, otherwise returns. If the application
   is pending (updated, not confirmed yet) the watchdog is left running: the
   application must call pjon_boot_app_ok() before it expires. */
static void start_app(void) {
  if(!app_present()) return;
  TCCR1B = 0;
  TCNT1 = 0;
  TIFR1 = 0xFF;
  BUS_RELEASE();
#if BOOT_GUARD_S
  if(app_pending()) wdt_enable(BOOT_GUARD_WDTO);
#endif
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

  uint16_t timeout = 0; // 0 = wait forever
  uint8_t reason = 0;   // announcement reason, 0 = do not announce
  page_address = NO_PAGE;
  last_page_ok = 1;
  flashed = 0;
  device_id = eeprom_read_byte((const uint8_t *)PJON_BOOT_EEPROM_ID_ADDR);
  if(!device_id || (device_id > 253)) device_id = DEVICE_ID;
  uint8_t watchdog = (reset_flags & (1 << WDRF)) &&
    (magic_0 == PJON_BOOT_MAGIC_0);
  if(watchdog && (magic_1 == PJON_BOOT_MAGIC_ENTER)) {
    if(id && (id < 254)) device_id = id;
    timeout = OVERFLOWS(TIMEOUT_MS);
  }
#if BOOT_GUARD_S
  else if((reset_flags & (1 << WDRF)) && app_pending())
    reason = PJON_BOOT_REASON_BOOT_FAILED; // stays, without timeout
#endif
#if EXTRF_TIMEOUT_MS
  else if(reset_flags & (1 << EXTRF)) timeout = OVERFLOWS(EXTRF_TIMEOUT_MS);
#endif
  else start_app(); // Returns only if there is no application

  BUS_RELEASE();
  TCCR1A = 0;
  load_serial();
  TCCR1B = (1 << CS11); // F_CPU / 8
  uint16_t remaining = timeout;
  uint16_t interval = OVERFLOWS(ANNOUNCE_FIRST_MS);
  uint16_t next_announce = 1 + (random8() & 31);

  for(;;) {
    if(TIFR1 & (1 << TOV1)) {
      TIFR1 = (1 << TOV1);
      if(timeout && !--remaining) {
        start_app();
        remaining = timeout; // No application, keep waiting
      }
      if(next_announce) next_announce--;
    }
    if(!BUS_HIGH()) {
      if(next_announce) continue;
      if(!reason && app_present()) {
        next_announce = OVERFLOWS(ANNOUNCE_MAX_MS);
        continue;
      }
      uint8_t result = announce(reason ? reason : PJON_BOOT_REASON_NO_APP);
      if(!result) next_announce = 1 + (random8() & 15); // bus busy
      else {
        next_announce = interval + (random8() & 31);
        if(interval < OVERFLOWS(ANNOUNCE_MAX_MS) / 2) interval <<= 1;
        else interval = OVERFLOWS(ANNOUNCE_MAX_MS);
      }
      continue;
    }
    /* The meta-data CRC8 is not checked: the CRC32 covers the whole packet
       (meta-data and CRC8 included) and the length is already bounded */
    uint32_t crc;
    uint8_t length = receive_frame(&crc);
    if(!length) continue;
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
    if(payload_length == 1) {
      /* Ping and go are answered only if the last page is in flash */
      if(
        (payload[0] == PJON_BOOT_CMD_PING) || (payload[0] == PJON_BOOT_CMD_GO)
      ) ok = last_page_ok;
    } else if(
      (payload_length == PJON_BOOT_WRITE_LENGTH) &&
      (payload[0] == PJON_BOOT_CMD_WRITE)
    ) ok = store_chunk((payload[1] << 8) | payload[2], payload + 3);
    else if(
      (payload_length == PJON_BOOT_ASSIGN_LENGTH) &&
      (payload[0] == PJON_BOOT_CMD_ASSIGN) &&
      (payload[1] == serial[0]) && (payload[2] == serial[1]) &&
      (payload[3] == serial[2]) && (payload[4] == serial[3]) &&
      payload[5] && (payload[5] < 254)
    ) ok = 3;
    if(!ok) continue; // No acknowledgement, the sender will retry
    if(header & PJON_ACK_REQ_BIT) send_ack();
    remaining = timeout;
    next_announce = OVERFLOWS(ANNOUNCE_MAX_MS); // the master is here
    if(ok == 2) finish_page(); // After the acknowledgement: no long wait
    else if(ok == 3) { // Assign: save the id after the acknowledgement
      device_id = payload[5];
      eeprom_update_byte((uint8_t *)PJON_BOOT_EEPROM_ID_ADDR, device_id);
    } else if(payload[0] == PJON_BOOT_CMD_GO) {
#if BOOT_GUARD_S
      if(flashed) eeprom_update_byte(EEPROM_FLAG, PJON_BOOT_FLAG_PENDING);
#endif
      start_app();
    }
  }
}
