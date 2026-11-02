/* PJON SoftwareBitBang bootloader for ATmega328P and ATmega2560
   ___________________________________________________________________________

   Minimal bootloader receiving a firmware image over a single wire PJON bus
   using the SoftwareBitBang strategy (PJDL v5.0). It is written in plain C
   without the PJON library or the Arduino core so that it fits in 2kB.
   The same source builds for the ATmega328P (and 88/168) and for the
   ATmega2560/1280 (MCU=atmega2560 in the Makefile): 256 bytes flash pages,
   addresses above 64kB written with the 'X' command (24 bits address).

   Supported (everything else is not needed to flash and is dropped):
   - PJDL frame reception and synchronous acknowledgement, one mode chosen
     at build time (MODE=1..6, default 1; 5 and 6 are faster modes of the
     SoftwareBitBang direct port access implementation), or the mode 7 link
     layer (MODE=7, not PJDL, see SoftwareBitBangM7.h)
   - PJON local mode packets addressed to the bootloader's device id
   - CRC32 packet verification (covers meta-data, the CRC8 is not checked)
   - Optional sender info and packet id (skipped), port PJON_BOOT_PORT
   - Commands: ping, write a page chunk (the page is erased, written and
     verified after its last chunk; 'X' with a 24 bits address on MCUs with
     more than 64kB of flash), start application, assign device id
     (see src/utils/bootloader/PJON_Bootloader.h for the protocol)
   - Announcements to the master while waiting without timeout (fresh
     device, failed application start), with medium access check
   - Device id and a random serial (generated on the first start if not
     written at ISP time) kept in EEPROM
   - Boot guard: the first start after an update is supervised by the
     watchdog until the application calls pjon_boot_app_ok()
   - Serial upload on USART0, if SERIAL_UPLOAD: compatible with Optiboot
     on the ATmega328P (STK500v1, Arduino IDE and avrdude -c arduino), with
     the Arduino Mega bootloader on the ATmega2560 (STK500v2, Arduino IDE
     and avrdude -c wiring)

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

#ifndef PJON_PIN          // Arduino pin number of the bus (328P: 0-19,
  #define PJON_PIN       12 // A0 = 14; Mega: pins of ports A-G, e.g. 3)
#endif
#ifndef DEVICE_ID         // Id used if none is passed or saved in EEPROM
  #define DEVICE_ID      PJON_BOOT_UNASSIGNED
#endif
#ifndef MODE              // SoftwareBitBang mode 1-7
  #define MODE            1
#endif
#ifndef TIMEOUT_MS        // Stay in the bootloader after an app request
  #define TIMEOUT_MS   5000
#endif
#ifndef EXTRF_TIMEOUT_MS  // Stay in the bootloader after the reset pin, 0=off
  #define EXTRF_TIMEOUT_MS 1000
#endif
#ifndef BOOT_START        // Start of the bootloader, flash above is protected
  #define BOOT_START 0x7400
#endif

/* Split layout (BOOT_SIZE=3072 in the Makefile): the hardware boot section
   is 2kB (0x7800-0x7FFF) and holds main() and the flash writing code (SPM
   only works from the boot section); functions marked LOWTEXT are placed in
   the 1kB below it (0x7400-0x77FF). The application can not write flash
   itself, and the bootloader refuses writes from BOOT_START up. */
#if LOW_SECTION
  #define LOWTEXT __attribute__((noinline, section(".bootlow")))
#else // not inlined: main() must not need a stack frame (see main())
  #define LOWTEXT __attribute__((noinline))
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

#ifndef SERIAL_UPLOAD     // Optiboot compatible serial upload on USART0
  #define SERIAL_UPLOAD   1
#endif
#ifndef SERIAL_BAUD
  #define SERIAL_BAUD 115200
#endif
#if SERIAL_UPLOAD && (PJON_PIN <= 1)
  #error "Pins 0 and 1 are the serial port: use another PJON_PIN or SERIAL_UPLOAD=0"
#endif

/* Announcements: first one within ~1s, then every 5s doubling up to 60s,
   plus up to ~1s of random jitter. A valid command postpones the next one
   by ANNOUNCE_MAX_MS. */
#define ANNOUNCE_FIRST_MS  5000
#define ANNOUNCE_MAX_MS   60000

/* Flash addresses: 16 bits up to 64kB of flash, 32 bits above (ATmega2560:
   the flash above 64kB is read with ELPM, written with the extended SPM
   macros of avr/boot.h) */
#if FLASHEND > 0xFFFF
  #define FAR_FLASH 1
  typedef uint32_t flash_address_t;
  #define FLASH_READ_BYTE(A) pgm_read_byte_far(A)
#else
  #define FAR_FLASH 0
  typedef uint16_t flash_address_t;
  #define FLASH_READ_BYTE(A) pgm_read_byte(A)
#endif
/* Index in a flash page (128 or 256 bytes) */
#if SPM_PAGESIZE > 128
  typedef uint16_t page_index_t;
#else
  typedef uint8_t page_index_t;
#endif

/* Pin mapping (Arduino numbering): the same table as the SoftwareBitBang
   direct port access implementation (ATmega328P pins 0-19, Mega pins of
   ports A-G) ------------------------------------------------------------- */

#define SWBB_PIN PJON_PIN
#include "strategies/SoftwareBitBang/SoftwareBitBangAVRPins.h"
#define BUS_PORT SWBB_PORT
#define BUS_DDR  SWBB_DDR
#define BUS_IN   SWBB_IN
#define BUS_BIT  SWBB_BIT

#define BUS_HIGH()     (BUS_IN & (1 << BUS_BIT))
#define BUS_DRIVE()    (BUS_DDR |= (1 << BUS_BIT))
#define BUS_SET()      (BUS_PORT |= (1 << BUS_BIT))
#define BUS_CLEAR()    (BUS_PORT &= ~(1 << BUS_BIT))
/* Input without pull-up, the bus pull-down resistor keeps it low */
#define BUS_RELEASE()  do { BUS_DDR &= ~(1 << BUS_BIT); BUS_CLEAR(); } while(0)
/* Release after transmitting: drive the bus low first, so the cable is
   discharged through the series resistor (see DISCHARGE_US) */
#define BUS_DISCHARGE() do { \
    BUS_CLEAR(); \
    __builtin_avr_delay_cycles(CYCLES(DISCHARGE_US)); \
    BUS_RELEASE(); \
  } while(0)

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
#elif MODE == 7
  /* Mode 7 is not PJDL (see src/strategies/SoftwareBitBang/
     SoftwareBitBangM7.h): these values are not used */
  #define PAD_DEFAULT_US  50
  #define BIT_DEFAULT_US   8
  #define ACC_DEFAULT_US  12
#else
  #error "MODE must be 1, 2, 3, 4, 5, 6 or 7"
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

/* Keep busy bit transmitted while waiting for an acknowledgement, as the
   SoftwareBitBang direct port access implementation (SWBB_WIRE_KEEP_BUSY),
   and the time the response is awaited after each one (SWBB_RESPONSE_LISTEN) */
#if MODE == 1
  #define KEEP_BUSY_US 9
#elif MODE == 2
  #define KEEP_BUSY_US 8
#elif MODE == 3
  #define KEEP_BUSY_US 7
#elif MODE == 4
  #define KEEP_BUSY_US 6.5
#else
  #define KEEP_BUSY_US 4
#endif
#define LISTEN_US 40

/* Time a transmitter drives the bus low before releasing it: the pull-down
   alone would take ~100us to discharge a long cable (SWBB_DISCHARGE_US) */
#define DISCHARGE_US 2

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
#if FAR_FLASH
  #define BUFFER_LENGTH (PJON_BOOT_MAX_OVERHEAD + PJON_BOOT_WRITE_FAR_LENGTH)
#else
  #define BUFFER_LENGTH (PJON_BOOT_MAX_OVERHEAD + PJON_BOOT_WRITE_LENGTH)
#endif
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
static flash_address_t page_address NOINIT;   // its address, NO_PAGE if none
static uint8_t next_chunk NOINIT;             // chunks received in order
static uint8_t last_page_ok NOINIT;           // result of the last page write
static uint8_t device_id NOINIT;
static uint8_t serial[4] NOINIT;              // copy of the EEPROM serial
static uint16_t random_state NOINIT;          // random8() state
static uint8_t flashed NOINIT;                // a page was written
static uint32_t frame_crc NOINIT;             // CRC32 of the received frame
#if MODE <= 6
static uint16_t byte_end NOINIT;              // Timer1 at the end of a byte
#endif

#define NO_PAGE ((flash_address_t)-1)

/* Error detection --------------------------------------------------------- */

/* CRC32 (PJON, IEEE 802.3). It is updated while a PJDL frame is received,
   one byte at a time at the start of the next padding bit: the padding bit
   is measured from the end of the previous byte, so this time is not lost
   (mode 7: computed after the frame, while the transmitter keeps the bus
   busy). The acknowledgement is sent right after the frame. */
#ifndef CRC32_TABLE
  #define CRC32_TABLE 0
#endif
#if CRC32_TABLE
/* 16 entries table (~6us per byte at 16MHz). In a .text section rather than
   PROGMEM: the linker places PROGMEM data before main(), which must stay at
   the reset address */
static const uint32_t crc32_table[16]
  __attribute__((used, section(".text.crc32_table"))) = {
  0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
  0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
  0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
  0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};

#if FAR_FLASH
  #error "CRC32_TABLE is not supported above 64kB of flash"
#endif
__attribute__((noinline))
static uint32_t crc32_update(uint32_t crc, uint8_t b) {
  crc ^= b;
  crc = (crc >> 4) ^ CRC32_TABLE(crc & 15);
  crc = (crc >> 4) ^ CRC32_TABLE(crc & 15);
  return crc;
}
#else
/* Bit by bit, in assembly (34 bytes instead of 100 compiled from C; ~100
   cycles per byte: ~6us at 16MHz, ~13us at 8MHz) */
__attribute__((noinline))
static uint32_t crc32_update(uint32_t crc, uint8_t b) {
  uint8_t i, p0, p1, p2, p3;
  __asm__ (
    "   eor  %A[c], %[b]   \n\t"
    "   ldi  %[i], 8       \n\t"
    "   ldi  %[p0], 0x20   \n\t"
    "   ldi  %[p1], 0x83   \n\t"
    "   ldi  %[p2], 0xB8   \n\t"
    "   ldi  %[p3], 0xED   \n\t"
    "1: lsr  %D[c]         \n\t"
    "   ror  %C[c]         \n\t"
    "   ror  %B[c]         \n\t"
    "   ror  %A[c]         \n\t"
    "   brcc 2f            \n\t"
    "   eor  %A[c], %[p0]  \n\t"
    "   eor  %B[c], %[p1]  \n\t"
    "   eor  %C[c], %[p2]  \n\t"
    "   eor  %D[c], %[p3]  \n\t"
    "2: dec  %[i]          \n\t"
    "   brne 1b            \n\t"
    : [c] "+r" (crc), [i] "=&d" (i), [p0] "=&d" (p0), [p1] "=&d" (p1),
      [p2] "=&d" (p2), [p3] "=&d" (p3)
    : [b] "r" (b)
  );
  return crc;
}
#endif

/* Meta-data CRC8 of the packets sent by the bootloader (PJON CRC8 C2) */
__attribute__((noinline))
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

#if MODE <= 6
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
   frame_crc the CRC32 of the packet without its last 4 bytes (the CRC32). */
LOWTEXT static uint8_t receive_frame(void) {
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
  frame_crc = ~c;
  return length;
}


/* SoftwareBitBang transmission ------------------------------------------- */

static void send_pad(void) {
  BUS_SET();
  BUS_DRIVE(); // Set high before driving, never pull against a high pulse
  __builtin_avr_delay_cycles(CYCLES_PAD);
  BUS_CLEAR();
  __builtin_avr_delay_cycles(CYCLES(TX_BIT_US));
}

/* Synchronization pad then 8 bits LSB first. The bit loop is written in
   assembly so its duration is exact: 11 cycles + 4 * BIT_LOOPS + BIT_NOPS =
   1 data bit (TX_BIT_US) */
static void send_byte(uint8_t value) {
  uint16_t n;
  uint8_t i = 8;
  send_pad();
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
  BUS_RELEASE(); // the last bit of PJON_ACK is 0: the bus is already low
}

/* Waits for the synchronous acknowledgement of a frame just sent: keeps
   the bus busy with short pulses, each one followed by the discharge of the
   bus, and listens LISTEN_US for the response after each one. The receiver
   answers after the falling edge of one of them with 2 synchronization pads
   and PJON_ACK. */
#define ACK_TIMEOUT_US 4000
__attribute__((noinline)) static uint8_t receive_ack(void) {
  uint16_t start = TCNT1;
  while((uint16_t)(TCNT1 - start) < TICKS(ACK_TIMEOUT_US)) {
    if(!BUS_HIGH()) { // Do not drive the bus if the response started
      BUS_SET();
      BUS_DRIVE();
      __builtin_avr_delay_cycles(CYCLES(KEEP_BUSY_US));
      BUS_DISCHARGE();
      uint16_t listen = TCNT1;
      while(!BUS_HIGH() && ((uint16_t)(TCNT1 - listen) < TICKS(LISTEN_US)));
    }
    if(BUS_HIGH()) return sync(0) && sync(1) && (read_byte() == PJON_ACK);
  }
  return 0;
}

/* Transmits a frame: frame initializer (3 synchronization pads) and the
   bytes, then discharges the bus (the last bit may be a 1). Returns 1. */
__attribute__((noinline))
static uint8_t transmit(const uint8_t *data, uint8_t length) {
  send_pad();
  send_pad();
  send_pad();
  for(uint8_t i = 0; i < length; i++) send_byte(data[i]);
  BUS_DISCHARGE();
  return 1;
}

/* The bus is free if it stays low for a padding bit and 10 data bits (a
   byte cannot be shorter) */
#define IDLE_US (PAD_US + 10 * BIT_US)

#else
/* Mode 7 (not PJDL) ------------------------------------------------------

   Same timing as src/strategies/SoftwareBitBang/SoftwareBitBangM7.h, the
   values must match the library's (SWBB_M7_*): a frame is a preamble (high,
   possibly extended by the transmitter's contention slot), a low bit and
   bytes made of a start bit (high), 8 data bits LSB first and a stop bit
   (low); after every M7_WINDOW bytes the transmitter keeps the bus low for
   its interrupt window. The CRC32 is computed after the frame, the
   transmitter keeps the bus busy meanwhile. */

#ifndef M7_BIT_US
  #define M7_BIT_US         4
#endif
#ifndef M7_PREAMBLE_US
  #define M7_PREAMBLE_US    (24 * M7_BIT_US)
#endif
#ifndef M7_PREAMBLE_MIN_US
  #define M7_PREAMBLE_MIN_US (11 * M7_BIT_US)
#endif
#ifndef M7_SLOTS_US                   // SWBB_M7_SLOTS * SWBB_M7_SLOT_STEP
  #define M7_SLOTS_US       32
#endif
#ifndef M7_WINDOW
  #define M7_WINDOW         4
#endif
#ifndef M7_GAP_US                     // SWBB_M7_GAP
  #define M7_GAP_US         48
#endif
#ifndef M7_GAP_MAX_US                 // SWBB_M7_GAP + 64
  #define M7_GAP_MAX_US     (48 + 64)
#endif
#ifndef M7_KEEP_BUSY_US
  #define M7_KEEP_BUSY_US   3
#endif
#ifndef M7_RESPONSE_DELAY_US
  #define M7_RESPONSE_DELAY_US 6
#endif
#ifndef M7_PULSE_WAIT_US
  #define M7_PULSE_WAIT_US  40
#endif
#ifndef M7_ACK_HIGH_US
  #define M7_ACK_HIGH_US    (4 * M7_BIT_US)
#endif
/* Cycles between the start bit's rising edge and the sampling point that
   are compensated (the bootloader's own value, the library uses
   SWBB_M7_ADVANCE): measured window 9-19 at 8MHz, 1-31 at 16MHz (200 of 200
   write chunks, m7_boot.sh) */
#ifndef M7_ADVANCE
  #define M7_ADVANCE        14
#endif

/* Edge waiting loops of exactly 6 cycles, return the iterations left */
#define LOOPS(us) ((uint16_t)(CYCLES(us) / 6))

static uint16_t wait_while_low(uint16_t loops) {
  __asm__ __volatile__ (
    "1: sbic %[in], %[bit]  \n\t"
    "   rjmp 2f             \n\t"
    "   sbiw %[n], 1        \n\t"
    "   brne 1b             \n\t"
    "2:                     \n\t"
    : [n] "+w" (loops)
    : [in] "I" (_SFR_IO_ADDR(BUS_IN)), [bit] "I" (BUS_BIT)
  );
  return loops;
}

/* Same as wait_while_low, always inlined: the start bit of a received byte
   (its timing is part of the sampling calibration, M7_ADVANCE) */
static inline __attribute__((always_inline)) uint16_t rx_wait_low(
  uint16_t loops
) {
  __asm__ __volatile__ (
    "1: sbic %[in], %[bit]  \n\t"
    "   rjmp 2f             \n\t"
    "   sbiw %[n], 1        \n\t"
    "   brne 1b             \n\t"
    "2:                     \n\t"
    : [n] "+w" (loops)
    : [in] "I" (_SFR_IO_ADDR(BUS_IN)), [bit] "I" (BUS_BIT)
  );
  return loops;
}

static uint16_t wait_while_high(uint16_t loops) {
  __asm__ __volatile__ (
    "1: sbis %[in], %[bit]  \n\t"
    "   rjmp 2f             \n\t"
    "   sbiw %[n], 1        \n\t"
    "   brne 1b             \n\t"
    "2:                     \n\t"
    : [n] "+w" (loops)
    : [in] "I" (_SFR_IO_ADDR(BUS_IN)), [bit] "I" (BUS_BIT)
  );
  return loops;
}

/* Waits for a start bit and receives the byte: returns > 255 on error */
static inline __attribute__((always_inline)) uint16_t rx_byte(
  uint16_t loops
) {
  if(!rx_wait_low(loops)) return 0x100;
  __builtin_avr_delay_cycles(CYCLES(M7_BIT_US * 1.5) - M7_ADVANCE);
  uint8_t value = 0, i = 8;
  uint16_t n;
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
      [loops] "i" (BIT_LOOPS(M7_BIT_US, 8)),
      [nops] "i" (BIT_NOPS(M7_BIT_US, 8))
  );
  /* A quarter of the stop bit: it must be low. Checked before its middle so
     the next start bit is awaited early enough also at 8MHz (a bit is then
     32 cycles and the code between two bytes takes ~20) */
  __builtin_avr_delay_cycles(CYCLES(M7_BIT_US * 0.75) - 4);
  if(BUS_HIGH()) return 0x200; // stop bit
  return value;
}

/* Receives a frame, called when the bus is high. Returns the packet length
   or 0, and in frame_crc the CRC32 of the packet without its last 4 bytes.
   In the boot section with rx_byte inlined, as the timing of its sampling
   (M7_ADVANCE) was measured (it matters at 8MHz: a bit is 32 cycles). Not
   inlined in main(): its registers would make main() use the stack before
   the mailbox is read. */
__attribute__((noinline)) static uint8_t receive_frame(void) {
  uint8_t length = BUFFER_LENGTH, i;
  uint16_t max = LOOPS(M7_PREAMBLE_US + M7_SLOTS_US + 20);
  uint16_t left = wait_while_high(max);
  if(!left || ((uint16_t)(max - left) < LOOPS(M7_PREAMBLE_MIN_US))) return 0;
  /* As little code as possible between two bytes: at 8MHz the next start
     bit comes ~8 cycles after the stop bit is checked. The id and the
     header are checked after the frame. */
  uint16_t wait = LOOPS(2 * M7_BIT_US);
  uint8_t *p = buffer, *end = buffer + 3, window = M7_WINDOW;
  for(;;) {
    uint16_t b = rx_byte(wait);
    if(b > 255) return 0;
    *p++ = b;
    if(p == end) {
      if(p == buffer + 3) { // length
        if((b > BUFFER_LENGTH) || (b < MIN_LENGTH)) return 0;
        end = buffer + b;
      } else break;
    }
    if(--window) wait = LOOPS((2 * M7_BIT_US) + 2);
    else {
      window = M7_WINDOW;
      wait = LOOPS(M7_BIT_US + M7_GAP_MAX_US + 8);
    }
  }
  length = p - buffer;
  if(buffer[0] != device_id) return 0;
  uint8_t h = buffer[1];
  if(h & (PJON_MODE_BIT | PJON_MAC_BIT | PJON_EXT_LEN_BIT)) return 0;
  if((h & (PJON_CRC_BIT | PJON_PORT_BIT)) != (PJON_CRC_BIT | PJON_PORT_BIT))
    return 0;
  uint32_t c = 0xFFFFFFFF;
  for(i = 0; i < length - 4; i++) c = crc32_update(c, buffer[i]);
  frame_crc = ~c;
  return length;
}

/* Transmits start bit, 8 data bits (LSB first) and stop bit, each one of
   exactly M7_BIT_US: 12 cycles + 4 * BIT_LOOPS + BIT_NOPS per bit (the bus
   is driven) */
static void tx_byte(uint8_t value) {
  uint16_t r = ((uint16_t)value << 1) | 1, n;
  uint8_t i = 10;
  __asm__ __volatile__ (
    "1: sbrc %A[r], 0             \n\t"
    "   sbi  %[port], %[bit]      \n\t"
    "   sbrs %A[r], 0             \n\t"
    "   cbi  %[port], %[bit]      \n\t"
    "   lsr  %B[r]                \n\t"
    "   ror  %A[r]                \n\t"
    "   ldi  %A[n], lo8(%[loops]) \n\t"
    "   ldi  %B[n], hi8(%[loops]) \n\t"
    "2: sbiw %[n], 1              \n\t"
    "   brne 2b                   \n\t"
    "   .rept %[nops]             \n\t"
    "   nop                       \n\t"
    "   .endr                     \n\t"
    "   dec  %[i]                 \n\t"
    "   brne 1b                   \n\t"
    : [r] "+r" (r), [i] "+r" (i), [n] "=&w" (n)
    : [port] "I" (_SFR_IO_ADDR(BUS_PORT)), [bit] "I" (BUS_BIT),
      [loops] "i" (BIT_LOOPS(M7_BIT_US, 12)),
      [nops] "i" (BIT_NOPS(M7_BIT_US, 12))
  );
}

/* Acknowledgement: synchronize to a keep busy pulse, then a high pulse, a
   low bit and the response byte (start, 8 bits, stop) */
static void send_ack(void) {
  wait_while_low(LOOPS(M7_PULSE_WAIT_US));
  wait_while_high(LOOPS(M7_KEEP_BUSY_US + 6));
  __builtin_avr_delay_cycles(CYCLES(M7_RESPONSE_DELAY_US));
  BUS_SET();
  BUS_DRIVE();
  __builtin_avr_delay_cycles(CYCLES(M7_ACK_HIGH_US));
  BUS_CLEAR();
  __builtin_avr_delay_cycles(CYCLES(M7_BIT_US) - 8);
  tx_byte(PJON_ACK);
  BUS_RELEASE(); // after the stop bit the bus is already low
}

/* Waits for the acknowledgement of a frame just sent (as the library's
   receive_response): keep busy pulses, each one followed by the discharge of
   the bus; the receiver answers after the falling edge of one of them with
   a high pulse, a low bit and PJON_ACK */
#define ACK_TIMEOUT_US 4000
#define M7_LISTEN_US     15
__attribute__((noinline)) static uint8_t receive_ack(void) {
  uint16_t start = TCNT1;
  while((uint16_t)(TCNT1 - start) < TICKS(ACK_TIMEOUT_US)) {
    if(!BUS_HIGH()) {
      BUS_SET();
      BUS_DRIVE();
      __builtin_avr_delay_cycles(CYCLES(M7_KEEP_BUSY_US));
      BUS_DISCHARGE();
      wait_while_low(LOOPS(M7_LISTEN_US));
    }
    if(BUS_HIGH()) {
      uint16_t max = LOOPS(M7_ACK_HIGH_US + 8);
      uint16_t left = wait_while_high(max);
      if(!left || ((uint16_t)(max - left) < LOOPS(M7_ACK_HIGH_US / 2)))
        return 0;
      return rx_byte(LOOPS(2 * M7_BIT_US)) == PJON_ACK;
    }
  }
  return 0;
}

/* Transmits a frame (as the library's tx_frame): a preamble extended by a
   random contention slot, then the bus is discharged, released and read: if
   it is high another device is transmitting, give up (returns 0). Then a
   low bit, the bytes, and a low interrupt window after every M7_WINDOW
   bytes. */
#define M7_SLOT_STEP_US 4
LOWTEXT static uint8_t transmit(const uint8_t *data, uint8_t length) {
  BUS_SET();
  BUS_DRIVE();
  __builtin_avr_delay_cycles(CYCLES(M7_PREAMBLE_US));
  for(uint8_t s = random8() & ((M7_SLOTS_US / M7_SLOT_STEP_US) - 1); s; s--)
    __builtin_avr_delay_cycles(CYCLES(M7_SLOT_STEP_US) - 4);
  BUS_CLEAR();
  __builtin_avr_delay_cycles(CYCLES(DISCHARGE_US));
  BUS_DDR &= ~(1 << BUS_BIT); // release and read
  __builtin_avr_delay_cycles(CYCLES(1));
  if(BUS_HIGH()) return 0;
  BUS_DRIVE(); // low again: the low bit before the first byte
  __builtin_avr_delay_cycles(CYCLES(M7_BIT_US) - CYCLES(DISCHARGE_US + 1));
  for(uint8_t i = 0; i < length; i++) {
    tx_byte(data[i]);
    if((((i + 1) % M7_WINDOW) == 0) && (i + 1 < length))
      __builtin_avr_delay_cycles(CYCLES(M7_GAP_US));
  }
  BUS_RELEASE(); // after the stop bit the bus is already low
  return 1;
}

/* The bus is free if it stays low this long (SWBB_M7_IDLE) */
#define IDLE_US ((11 * M7_BIT_US) + M7_GAP_MAX_US + 8)

#endif

/* Announces the bootloader to the master. Returns 0 if the bus is busy (try
   again soon), 1 if the announcement was not acknowledged, 2 if it was. */
LOWTEXT static uint8_t announce(uint8_t reason) {
  /* Medium access: the bus must stay low for IDLE_US and a random time */
  uint16_t start = TCNT1;
  uint16_t wait = TICKS(IDLE_US) + (random8() & 127);
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
  if(!transmit(buffer, p - buffer)) return 0; // mode 7: another frame
  return receive_ack() + 1;
}

/* EEPROM ------------------------------------------------------------------ */

/* Loads the serial from EEPROM. On the first start (erased EEPROM) a random
   serial is generated from the jitter between the watchdog oscillator and
   the CPU clock, and saved. The first byte of a serial is never 0xFF. */
#define EEPROM_SERIAL ((uint8_t *)PJON_BOOT_EEPROM_SERIAL_ADDR)
LOWTEXT static void load_serial(void) {
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

static uint8_t page_matches(flash_address_t address, const uint8_t *data) {
  for(page_index_t i = 0; i < SPM_PAGESIZE; i++)
    if(FLASH_READ_BYTE(address + i) != data[i]) return 0;
  return 1;
}

/* Erases, writes and verifies a page. Returns 1 if the page content in flash
   is correct. The bootloader section can not be overwritten. */
static uint8_t write_page(flash_address_t address, const uint8_t *data) {
  if((address & (SPM_PAGESIZE - 1)) || (address >= BOOT_START)) return 0;
  if(page_matches(address, data)) return 1;
  eeprom_busy_wait(); // SPM is not possible while the EEPROM is written
  boot_page_erase(address);
  boot_spm_busy_wait();
  for(page_index_t i = 0; i < SPM_PAGESIZE; i += 2)
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
static uint8_t store_chunk(flash_address_t address, const uint8_t *data) {
  if((address & (PJON_BOOT_CHUNK_SIZE - 1)) || (address >= BOOT_START))
    return 0;
  flash_address_t base = address & ~(flash_address_t)(SPM_PAGESIZE - 1);
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
static void start_app(void);

/* Serial upload (STK500v1 subset, like Optiboot) ------------------------- */

#if SERIAL_UPLOAD
#define STK_OK          0x10
#define STK_FAILED      0x11
#define STK_INSYNC      0x14
#define CRC_EOP         0x20
#define STK_GET_SYNC    '0'
#define STK_GET_PARAM   'A'
#define STK_SET_DEVICE  'B'
#define STK_SET_DEV_EXT 'E'
#define STK_LEAVE       'Q'
#define STK_LOAD_ADDR   'U'
#define STK_UNIVERSAL   'V'
#define STK_PROG_PAGE   'd'
#define STK_READ_PAGE   't'
#define STK_READ_SIGN   'u'
#define STK_SW_MAJOR    0x81
#define STK_SW_MINOR    0x82

/* RX0 pin: PD0 on the ATmega328P, PE0 on the ATmega2560 */
#if FAR_FLASH
  #define SERIAL_RX_PORT PORTE
#else
  #define SERIAL_RX_PORT PORTD
#endif
#define SERIAL_RX_MASK 1

static void serial_init(void) {
  SERIAL_RX_PORT |= SERIAL_RX_MASK; // pull-up on RX: no noise when idle
  UCSR0A = (1 << U2X0);
  UBRR0 = (F_CPU + 4UL * SERIAL_BAUD) / (8UL * SERIAL_BAUD) - 1;
  UCSR0B = (1 << RXEN0) | (1 << TXEN0);
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

static void putch(uint8_t c) {
  while(!(UCSR0A & (1 << UDRE0)));
  UDR0 = c;
}

/* The watchdog (1s) is reset by each received byte: if the upload stops the
   device is reset, like with Optiboot */
static uint8_t getch(void) {
  while(!(UCSR0A & (1 << RXC0)));
  wdt_reset();
  return UDR0;
}

#if !FAR_FLASH
#define SERIAL_START STK_GET_SYNC

static void skip(uint8_t count) {
  while(count--) getch();
}

/* Every command ends with CRC_EOP. Out of sync: reset (Optiboot does the
   same), the host retries the synchronization. */
static void insync(void) {
  if(getch() != CRC_EOP) {
    wdt_enable(WDTO_15MS);
    for(;;);
  }
  putch(STK_INSYNC);
}

/* Serves an STK500v1 session started by the byte c. Does not return: it
   ends starting the application (STK_LEAVE) or with a watchdog reset. */
LOWTEXT static void serial_session(uint8_t c) __attribute__((noreturn));
static void serial_session(uint8_t c) {
  uint16_t address = 0;
  uint8_t written = 0;
  wdt_enable(WDTO_1S);
  for(;; c = getch()) {
    uint8_t ok = 1;
    if(c == STK_GET_PARAM) {
      uint8_t which = getch();
      insync();
      putch(which == STK_SW_MAJOR ? 8 : 3); // version 8.3, like Optiboot
    } else if(c == STK_SET_DEVICE) {
      skip(20);
      insync();
    } else if(c == STK_SET_DEV_EXT) {
      skip(5);
      insync();
    } else if(c == STK_LOAD_ADDR) {
      address = getch();
      address |= getch() << 8;
      address <<= 1; // word address
      insync();
    } else if(c == STK_UNIVERSAL) {
      skip(4);
      insync();
      putch(0);
    } else if(c == STK_PROG_PAGE) {
      getch(); // length high byte, always 0
      uint8_t length = getch(), type = getch();
      for(uint8_t i = 0; i < length; i++) {
        uint8_t b = getch();
        if(i < SPM_PAGESIZE) page[i] = b;
      }
      insync();
      /* Flash only, full pages; the bootloader can not be overwritten */
      ok = (type == 'F') && (length == SPM_PAGESIZE) &&
        write_page(address, page);
      written |= ok;
    } else if(c == STK_READ_PAGE) {
      getch();
      uint8_t length = getch(), type = getch();
      insync();
      while(length--) putch((type == 'F') ? pgm_read_byte(address++) : 0xFF);
    } else if(c == STK_READ_SIGN) {
      insync();
      putch(SIGNATURE_0);
      putch(SIGNATURE_1);
      putch(SIGNATURE_2);
    } else if(c == STK_LEAVE) {
      insync();
      putch(STK_OK);
      while(!(UCSR0A & (1 << UDRE0)));
      /* A serial upload does not use the boot guard (Arduino sketches do
         not call pjon_boot_app_ok()) */
      if(written) eeprom_update_byte(EEPROM_FLAG, 0xFF);
      __builtin_avr_delay_cycles(CYCLES(200)); // last byte out
      wdt_disable();
      start_app();
      wdt_enable(WDTO_15MS); // no application: restart the bootloader
      for(;;);
    } else insync(); // GET_SYNC, ENTER_PROGMODE and anything else
    putch(ok ? STK_OK : STK_FAILED);
  }
}
#else
/* Serial upload, STK500v2 subset (ATmega2560: Arduino IDE and avrdude -c
   wiring, like the Arduino Mega bootloader). A message is 0x1B seq sizeH
   sizeL 0x0E body[size] checksum (XOR of all the previous bytes); the
   answer uses the same sequence number. */

#define STK2_START            0x1B
#define SERIAL_START          STK2_START
#define STK2_TOKEN            0x0E
#define STK2_SIGN_ON          0x01
#define STK2_SET_PARAMETER    0x02
#define STK2_GET_PARAMETER    0x03
#define STK2_LOAD_ADDRESS     0x06
#define STK2_ENTER_PROGMODE   0x10
#define STK2_LEAVE_PROGMODE   0x11
#define STK2_CHIP_ERASE       0x12
#define STK2_PROGRAM_FLASH    0x13
#define STK2_READ_FLASH       0x14
#define STK2_PROGRAM_EEPROM   0x15
#define STK2_READ_EEPROM      0x16
#define STK2_READ_FUSE        0x18
#define STK2_READ_LOCK        0x1A
#define STK2_READ_SIGNATURE   0x1B
#define STK2_SPI_MULTI        0x1D
#define STK2_OK               0x00
#define STK2_FAILED           0xC0
#define STK2_UNKNOWN          0xC9
#define STK2_PARAM_HW_VER     0x90
#define STK2_PARAM_SW_MAJOR   0x91
#define STK2_PARAM_SW_MINOR   0x92
#define STK2_PARAM_VTARGET    0x94

/* First body bytes of a message: command and up to 9 parameters; the data
   of a flash write goes straight into page[] */
static uint8_t stk2_head[10] NOINIT;
static uint8_t stk2_sum NOINIT;

static void stk2_put(uint8_t c) {
  stk2_sum ^= c;
  putch(c);
}

static void stk2_answer_start(uint8_t seq, uint16_t size) {
  stk2_sum = 0;
  stk2_put(STK2_START);
  stk2_put(seq);
  stk2_put(size >> 8);
  stk2_put(size & 0xFF);
  stk2_put(STK2_TOKEN);
}

/* Reads a whole message: returns its size (0 if malformed), the first 10
   body bytes in stk2_head, the rest (flash data) in page[] */
static uint16_t stk2_receive(uint8_t first, uint8_t *seq) {
  while(first != STK2_START) first = getch(); // resynchronize
  uint8_t sum = first, c;
  *seq = getch();
  uint16_t size = getch();
  size = (size << 8) | getch();
  c = getch();
  sum ^= *seq ^ (size >> 8) ^ (size & 0xFF) ^ c;
  if(c != STK2_TOKEN) return 0;
  for(uint16_t i = 0; i < size; i++) {
    c = getch();
    sum ^= c;
    if(i < sizeof(stk2_head)) stk2_head[i] = c;
    else if(i - sizeof(stk2_head) < SPM_PAGESIZE)
      page[i - sizeof(stk2_head)] = c;
  }
  if(getch() != sum) return 0;
  return size;
}

/* Serves an STK500v2 session started by the byte c. Does not return: it
   ends starting the application (LEAVE_PROGMODE) or with a watchdog
   reset (no byte received for 1s). */
LOWTEXT static void serial_session(uint8_t c) __attribute__((noreturn));
static void serial_session(uint8_t c) {
  uint32_t loaded = 0;  // last LOAD_ADDRESS value (word address, EEPROM)
  uint32_t address = 0; // byte address (flash), auto incremented
  uint8_t written = 0, seq;
  wdt_enable(WDTO_1S);
  for(;; c = getch()) {
    if(!stk2_receive(c, &seq)) continue; // bad message: the host retries
    uint8_t cmd = stk2_head[0], status = STK2_OK, leave = 0;
    uint16_t length = ((uint16_t)stk2_head[1] << 8) | stk2_head[2];
    switch(cmd) {
      case STK2_SIGN_ON: // "AVRISP_2", what avrdude -c wiring expects
        stk2_answer_start(seq, 3 + 8);
        stk2_put(cmd); stk2_put(STK2_OK); stk2_put(8);
        stk2_put('A'); stk2_put('V'); stk2_put('R'); stk2_put('I');
        stk2_put('S'); stk2_put('P'); stk2_put('_'); stk2_put('2');
        stk2_put(stk2_sum);
        continue;
      case STK2_GET_PARAMETER: {
        uint8_t p = stk2_head[1], v = 0;
        if(p == STK2_PARAM_HW_VER) v = 0x0F;
        else if(p == STK2_PARAM_SW_MAJOR) v = 2;
        else if(p == STK2_PARAM_SW_MINOR) v = 0x0A;
        else if(p == STK2_PARAM_VTARGET) v = 50;
        stk2_answer_start(seq, 3);
        stk2_put(cmd); stk2_put(STK2_OK); stk2_put(v);
        stk2_put(stk2_sum);
        continue;
      }
      case STK2_LOAD_ADDRESS:
        loaded = ((uint32_t)stk2_head[1] << 24) |
          ((uint32_t)stk2_head[2] << 16) |
          ((uint16_t)stk2_head[3] << 8) | stk2_head[4];
        address = loaded << 1; // bit 31 (extended address flag) shifted out
        break;
      case STK2_PROGRAM_FLASH:
        /* Full pages only, written and verified; not the bootloader */
        if((length == SPM_PAGESIZE) && write_page(address, page)) written = 1;
        else status = STK2_FAILED;
        address += length;
        break;
      case STK2_READ_FLASH:
      case STK2_READ_EEPROM:
        stk2_answer_start(seq, length + 3);
        stk2_put(cmd); stk2_put(STK2_OK);
        for(uint16_t i = 0; i < length; i++) {
          if(cmd == STK2_READ_FLASH) stk2_put(pgm_read_byte_far(address++));
          else stk2_put(eeprom_read_byte((uint8_t *)(uint16_t)(loaded++)));
        }
        stk2_put(STK2_OK);
        stk2_put(stk2_sum);
        continue;
      case STK2_READ_SIGNATURE:
      case STK2_READ_FUSE:
      case STK2_READ_LOCK:
      case STK2_SPI_MULTI: {
        /* ISP instruction in stk2_head[2..5] (SPI_MULTI: [4..7]) */
        uint8_t *i = stk2_head + ((cmd == STK2_SPI_MULTI) ? 4 : 2), v = 0;
        if(i[0] == 0x30) // signature byte i[2]
          v = (i[2] == 0) ? SIGNATURE_0 :
            ((i[2] == 1) ? SIGNATURE_1 : SIGNATURE_2);
        else if((i[0] == 0x50) && (i[1] == 0x00))
          v = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
        else if((i[0] == 0x58) && (i[1] == 0x08))
          v = boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
        else if((i[0] == 0x50) && (i[1] == 0x08))
          v = boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
        else if((i[0] == 0x58) && (i[1] == 0x00))
          v = boot_lock_fuse_bits_get(GET_LOCK_BITS);
        if(cmd == STK2_SPI_MULTI) { // answer: rx bytes, the 4th is the value
          stk2_answer_start(seq, 7);
          stk2_put(cmd); stk2_put(STK2_OK);
          stk2_put(i[0]); stk2_put(i[1]); stk2_put(i[2]); stk2_put(v);
          stk2_put(STK2_OK);
        } else {
          stk2_answer_start(seq, 4);
          stk2_put(cmd); stk2_put(STK2_OK); stk2_put(v); stk2_put(STK2_OK);
        }
        stk2_put(stk2_sum);
        continue;
      }
      case STK2_LEAVE_PROGMODE:
        leave = 1;
        break;
      case STK2_SET_PARAMETER:
      case STK2_ENTER_PROGMODE:
      case STK2_CHIP_ERASE:     // pages are rewritten whole, like Arduino's
      case STK2_PROGRAM_EEPROM: // EEPROM not written (as the 328P version)
        break;
      default:
        status = STK2_UNKNOWN;
    }
    stk2_answer_start(seq, 2);
    stk2_put(cmd);
    stk2_put(status);
    stk2_put(stk2_sum);
    if(leave) {
      while(!(UCSR0A & (1 << UDRE0)));
      /* A serial upload does not use the boot guard */
      if(written) eeprom_update_byte(EEPROM_FLAG, 0xFF);
      __builtin_avr_delay_cycles(CYCLES(200)); // last byte out
      wdt_disable();
      start_app();
      wdt_enable(WDTO_15MS); // no application: restart the bootloader
      for(;;);
    }
  }
}
#endif
#endif

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
#if SERIAL_UPLOAD
  UCSR0B = 0;
  SERIAL_RX_PORT &= ~SERIAL_RX_MASK;
#endif
#if BOOT_GUARD_S
  if(app_pending()) wdt_enable(BOOT_GUARD_WDTO);
#endif
#if FAR_FLASH
  RAMPZ = 0;
  #ifdef EIND
    EIND = 0;
  #endif
#endif
  /* Reset flags also in r2, like Optiboot */
  __asm__ __volatile__ (
    "in r2, %0" "\n\t"
    "jmp 0" :: "I" (_SFR_IO_ADDR(GPIOR0))
  );
}

/* Main -------------------------------------------------------------------- */

int main(void) __attribute__((OS_main, section(".init9")));

/* main() must not need a stack frame: the compiler would reserve it with
   pushes (rcall) before the mailbox at the top of SRAM is read, destroying
   it. Do not take the address of local variables here. */
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
    reason = PJON_BOOT_REASON_REQUESTED; // tell the sender we are here
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
#if SERIAL_UPLOAD
  serial_init();
#endif
  uint16_t remaining = timeout;
  uint16_t interval = OVERFLOWS(ANNOUNCE_FIRST_MS);
  uint16_t next_announce = 1 + (random8() & 31);

  for(;;) {
#if SERIAL_UPLOAD
    if((UCSR0A & (1 << RXC0)) && (UDR0 == SERIAL_START))
      serial_session(SERIAL_START);
#endif
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
    uint8_t length = receive_frame();
    if(!length) continue;
    uint32_t crc = frame_crc;
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
    ) ok = store_chunk( // 16 bits: no sign extension of int (16 bits)
      (uint16_t)((payload[1] << 8) | payload[2]), payload + 3
    );
#if FAR_FLASH
    else if(
      (payload_length == PJON_BOOT_WRITE_FAR_LENGTH) &&
      (payload[0] == PJON_BOOT_CMD_WRITE_FAR)
    ) ok = store_chunk(
      ((flash_address_t)payload[1] << 16) |
      (uint16_t)((payload[2] << 8) | payload[3]),
      payload + 4
    );
#endif
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
