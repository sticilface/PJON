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
     verified after its last chunk), start application (see
     src/utils/bootloader/PJON_Bootloader.h for the protocol)

   Not supported: shared mode, MAC, extended length, broadcast, routing,
   transmitting packets and medium access (it never starts a transmission,
   it only answers with an acknowledgement), receiving while a page is being
   written (the sender's ping is retried by PJON until the write is over).

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
  #define BOOT_START 0x7800
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

/* Modes 5 and 6 are faster modes supported only by this bootloader and the
   SoftwareBitBang direct port access implementation (SWBB_PIN), see
   src/strategies/SoftwareBitBang/Timing.h. Each value can be overridden at
   build time (-DPAD_US=... etc.), it must then match the other devices. */
#ifndef MODE5_PAD_US
  #define MODE5_PAD_US 50
  #define MODE5_BIT_US 12
  #define MODE5_ACC_US 16
#endif
#ifndef MODE6_PAD_US
  #define MODE6_PAD_US 50
  #define MODE6_BIT_US  8
  #define MODE6_ACC_US 12
#endif
#if MODE == 1
  #define PAD_DEFAULT_US 110
  #define BIT_DEFAULT_US  44
  #define ACC_DEFAULT_US  56
#elif MODE == 2
  #define PAD_DEFAULT_US  92
  #define BIT_DEFAULT_US  40
  #define ACC_DEFAULT_US  56
#elif MODE == 3
  #define PAD_DEFAULT_US  70
  #define BIT_DEFAULT_US  28
  #define ACC_DEFAULT_US  30
#elif MODE == 4
  #define PAD_DEFAULT_US  65
  #define BIT_DEFAULT_US  26
  #define ACC_DEFAULT_US  30
#elif MODE == 5
  #define PAD_DEFAULT_US  MODE5_PAD_US
  #define BIT_DEFAULT_US  MODE5_BIT_US
  #define ACC_DEFAULT_US  MODE5_ACC_US
#elif MODE == 6
  #define PAD_DEFAULT_US  MODE6_PAD_US
  #define BIT_DEFAULT_US  MODE6_BIT_US
  #define ACC_DEFAULT_US  MODE6_ACC_US
#else
  #error "MODE must be 1, 2, 3, 4, 5 or 6"
#endif
#ifndef PAD_US
  #define PAD_US PAD_DEFAULT_US
#endif
#ifndef BIT_US
  #define BIT_US BIT_DEFAULT_US
#endif
#ifndef ACC_US
  #define ACC_US ACC_DEFAULT_US
#endif

/* Data bit duration expected when receiving and transmitted (the ACK), the
   same as the SoftwareBitBang direct port access implementation (SWBB_RX_BIT
   and SWBB_TX_BIT in Timing.h): devices using digitalRead sample with a
   period slightly longer than BIT_US, transmitting BIT_US + 1 suits both
   them and the exact implementations; receiving at BIT_US + 0.5 is in the
   middle of what the different implementations transmit. */
#if MODE >= 5 // only exact implementations support modes 5 and 6
  #ifndef RX_BIT_US
    #define RX_BIT_US BIT_US
  #endif
  #ifndef TX_BIT_US
    #define TX_BIT_US BIT_US
  #endif
#endif
#ifndef RX_BIT_US
  #define RX_BIT_US (BIT_US + 0.5)
#endif

/* Cycles executed between the detection of the falling edge of a padding bit
   and the sampling of the first data bit, compensated in modes 5 and 6 only
   (modes 1-4 were validated without compensation) */
#ifndef SAMPLE_ADVANCE
  #if MODE >= 5
    #define SAMPLE_ADVANCE 40
  #else
    #define SAMPLE_ADVANCE 0
  #endif
#endif
#ifndef TX_BIT_US
  #define TX_BIT_US (BIT_US + 1)
#endif

/* Longest acceptable padding bit (PJDL tolerance is +17us) */
#define PAD_MAX_US (PAD_US + 20)

/* Timer1 runs at F_CPU / 8 and is used to measure durations */
#define TICKS(us) ((uint16_t)((us) * (F_CPU / 1000000.0) / 8))
#define CYCLES(us) ((uint32_t)((us) * (F_CPU / 1000000.0)))
#define CYCLES_PAD CYCLES(PAD_US)

/* Data bit loops (assembly): OVERHEAD cycles + 4 per iteration + nops */
#define BIT_LOOPS(BIT, OVERHEAD) ((CYCLES(BIT) - (OVERHEAD)) / 4)
#define BIT_NOPS(BIT, OVERHEAD) ((CYCLES(BIT) - (OVERHEAD)) % 4)

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
static uint8_t buffer[BUFFER_LENGTH] NOINIT;  // received packet
static uint8_t page[SPM_PAGESIZE] NOINIT;     // page being assembled
static uint16_t page_address NOINIT;          // its address, NO_PAGE if none
static uint8_t next_chunk NOINIT;             // chunks received in order
static uint8_t last_page_ok NOINIT;           // result of the last page write
static uint16_t byte_end NOINIT;              // Timer1 at the end of a byte
static uint8_t device_id NOINIT;

#define NO_PAGE 0xFFFF

/* Error detection --------------------------------------------------------- */

/* CRC32 (PJON, IEEE 802.3) using a 16 entries table. It is updated while
   the frame is received, one byte at a time at the start of the next padding
   bit (~6us at 16MHz, ~12us at 8MHz; the padding bit is measured from the
   end of the previous byte, so this time is not lost), so the
   acknowledgement can be sent right after the frame, well within the default
   PJON response timeout. */
/* In a .text section rather than PROGMEM: the linker places PROGMEM data
   before main(), which must stay at the reset address (BOOT_START) */
static const uint32_t crc32_table[16]
  __attribute__((used, section(".text.crc32_table"))) = {
  0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
  0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
  0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
  0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};

static uint32_t crc32_update(uint32_t crc, uint8_t b) {
  crc ^= b;
  crc = (crc >> 4) ^ pgm_read_dword(&crc32_table[crc & 15]);
  crc = (crc >> 4) ^ pgm_read_dword(&crc32_table[crc & 15]);
  return crc;
}

/* SoftwareBitBang reception ----------------------------------------------- */

/* Synchronization pad: a high padding bit followed by a low data bit.
   Returns 1 at the end of the low bit if the pattern is recognised.
   If the bus is still low (the previous bit was a 0 slightly longer than
   expected) the rising edge is awaited for up to one bit. If continued, the
   padding bit started at the end of the previous byte or pad (byte_end): if
   the bus is already high it is measured from there, so the time spent
   updating the CRC32 between bytes is not subtracted from it. */
static uint8_t sync(uint8_t continued) {
  uint16_t start = TCNT1, elapsed;
  BUS_RELEASE();
  if(BUS_HIGH()) {
    if(continued) start = byte_end;
  } else {
    while(!BUS_HIGH())
      if((uint16_t)(TCNT1 - start) > TICKS(RX_BIT_US)) return 0;
    start = TCNT1;
  }
  do elapsed = TCNT1 - start;
  while(BUS_HIGH() && (elapsed <= TICKS(PAD_MAX_US)));
  if((elapsed < TICKS(ACC_US)) || (elapsed > TICKS(PAD_MAX_US))) return 0;
  __builtin_avr_delay_cycles((CYCLES(RX_BIT_US) / 2) - (SAMPLE_ADVANCE / 2));
  if(BUS_HIGH()) return 0;
  __builtin_avr_delay_cycles(CYCLES(RX_BIT_US) / 2);
  byte_end = TCNT1;
  return 1;
}

/* Reads 8 bits LSB first sampling in the middle of each bit. The bit loop is
   written in assembly so its duration is exact:
   8 cycles + 4 * BIT_LOOPS + BIT_NOPS = 1 data bit */
static uint8_t read_byte(void) {
  uint8_t value = 0, i = 8;
  uint16_t n;
  __builtin_avr_delay_cycles((CYCLES(RX_BIT_US) / 2) - (SAMPLE_ADVANCE / 2));
  __asm__ __volatile__ (
    "1: lsr  %[v]                 \n\t"
    "   sbic %[in], %[bit]        \n\t"
    "   ori  %[v], 0x80           \n\t"
    "   dec  %[i]                 \n\t"
    "   breq 3f                   \n\t"
    "   ldi  %A[n], lo8(%[loops]) \n\t"
    "   ldi  %B[n], hi8(%[loops]) \n\t"
    "2: sbiw %[n], 1              \n\t"
    "   brne 2b                   \n\t"
    "   .rept %[nops]             \n\t"
    "   nop                       \n\t"
    "   .endr                     \n\t"
    "   rjmp 1b                   \n\t"
    "3:                           \n\t"
    : [v] "+d" (value), [i] "+r" (i), [n] "=&w" (n)
    : [in] "I" (_SFR_IO_ADDR(BUS_IN)), [bit] "I" (BUS_BIT),
      [loops] "i" (BIT_LOOPS(RX_BIT_US, 8)),
      [nops] "i" (BIT_NOPS(RX_BIT_US, 8))
  );
  __builtin_avr_delay_cycles(CYCLES(RX_BIT_US) / 2);
  byte_end = TCNT1;
  return value;
}

/* Receives a frame (3 sync pads then sync pad + byte for each byte).
   Called when the bus is high. Returns the packet length or 0, and in
   *crc the CRC32 of the packet without its last 4 bytes (the CRC32). */
static uint8_t receive_frame(uint32_t *crc) {
  uint8_t length = BUFFER_LENGTH, i;
  uint32_t c = 0xFFFFFFFF;
  if(!sync(0) || !sync(1) || !sync(1)) return 0;
  for(i = 0; i < length; i++) {
    if(!sync(1)) return 0;
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

/* SoftwareBitBang synchronous acknowledgement ----------------------------- */

static void send_pad(void) {
  BUS_SET();
  BUS_DRIVE(); // Set high before driving, never pull against a high pulse
  __builtin_avr_delay_cycles(CYCLES_PAD);
  BUS_CLEAR();
  __builtin_avr_delay_cycles(CYCLES(TX_BIT_US));
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
  uint16_t start = TCNT1, n;
  uint8_t value = PJON_ACK, i = 8;
  while(!BUS_HIGH() && ((uint16_t)(TCNT1 - start) < TICKS(PULSE_WAIT_US)));
  start = TCNT1;
  while(BUS_HIGH() && ((uint16_t)(TCNT1 - start) < TICKS(BIT_US / 2)));
  __builtin_avr_delay_cycles(CYCLES(RESPONSE_DELAY_US));
  send_pad();
  send_pad();
  /* 11 cycles + 4 * BIT_LOOPS + BIT_NOPS = 1 data bit */
  __asm__ __volatile__ (
    "1: sbrc %[b], 0              \n\t"
    "   sbi  %[port], %[bit]      \n\t"
    "   sbrs %[b], 0              \n\t"
    "   cbi  %[port], %[bit]      \n\t"
    "   lsr  %[b]                 \n\t"
    "   ldi  %A[n], lo8(%[loops]) \n\t"
    "   ldi  %B[n], hi8(%[loops]) \n\t"
    "2: sbiw %[n], 1              \n\t"
    "   brne 2b                   \n\t"
    "   .rept %[nops]             \n\t"
    "   nop                       \n\t"
    "   .endr                     \n\t"
    "   dec  %[i]                 \n\t"
    "   brne 1b                   \n\t"
    : [b] "+r" (value), [i] "+r" (i), [n] "=&w" (n)
    : [port] "I" (_SFR_IO_ADDR(BUS_PORT)), [bit] "I" (BUS_BIT),
      [loops] "i" (BIT_LOOPS(TX_BIT_US, 11)),
      [nops] "i" (BIT_NOPS(TX_BIT_US, 11))
  );
  BUS_RELEASE();
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
  page_address = NO_PAGE;
  last_page_ok = 1;
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
    if(!ok) continue; // No acknowledgement, the sender will retry
    if(header & PJON_ACK_REQ_BIT) send_ack();
    remaining = timeout;
    if(ok == 2) finish_page(); // After the acknowledgement: no long wait
    else if(payload[0] == PJON_BOOT_CMD_GO) start_app();
  }
}
