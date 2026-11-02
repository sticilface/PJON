/* SoftwareBitBang mode 7 (prototype, not PJDL compatible on the wire)
   ___________________________________________________________________________

   Selected with SWBB_MODE 7 and SWBB_PIN (ATmega88/168/328), optionally with
   SWBB_INTERRUPT_WAKE (pin 2 or 3). Every device of the bus must use mode 7
   (devices in modes 1-6 do not understand it, and it does not understand
   them). The PJON packet format and the application API do not change.

   Frame (the bus is low when idle, T = SWBB_M7_BIT, default 4us):
    _______________________ ___ _______________________ ____________ ___
   | PREAMBLE    | SLOT    |   | BYTE                  | BYTE       |...
   |_____________|_________|   |_      ___     _______ |_           |
   | high                  |low|S|d0 ... d7 (LSB first)|P| S ...     |
   | SWBB_M7_PREAMBLE + 0-28us | T |1|                   |0|          |
                               ^ every byte: start bit (high), 8 data bits,
                                 stop bit (low): an UART with inverted levels
   - The preamble is longer than the longest high level inside a frame (a
     start bit and 8 data bits at 1 = 9T): a receiver recognises it if at
     least SWBB_M7_PREAMBLE_MIN of it is left when it starts listening, so it
     can never start receiving in the middle of a frame.
   - Contention slot: the transmitter extends the preamble by a random 0-7
     steps of 4us, then drives the bus low for SWBB_DISCHARGE_US, releases it
     and reads it: if it is high another device started at the same time
     with a longer preamble, this one gives up (PJON retries later) and the
     other frame is not disturbed.
   - The receiver synchronizes to the rising edge of every start bit: the
     sampling error accumulates over 9.5 bits, the devices' clocks may differ
     by about +-3.5% (measured), crystals and resonators are within +-0.5%.
   - After every SWBB_M7_WINDOW bytes the transmitter keeps the bus low for
     SWBB_M7_GAP microseconds: both devices serve pending interrupts there
     (interrupts are disabled during the rest of the frame). The time the
     transmitter spends in interrupts is compensated with Timer0, if they
     take longer the window gets longer (up to SWBB_M7_GAP_MAX accepted). A
     receiver whose interrupts take longer than SWBB_M7_GAP misses the next
     start bit and drops the frame (the transmitter retries).
   - The frame ends after the number of bytes in its PJON header (no end
     marker), it is handed to PJON at once (one receive_frame call).
   - Idle: the bus is considered free after SWBB_M7_IDLE microseconds low (the
     longest low level inside a frame is 9T + SWBB_M7_GAP_MAX).

   Synchronous acknowledgement (same scheme as PJDL, shorter):
   - The transmitter keeps the bus busy with SWBB_M7_KEEP_BUSY high pulses,
     after each one it listens SWBB_M7_LISTEN microseconds for a response.
   - The receiver (after PJON checked the CRC, or the wake interrupt) waits
     for the falling edge of a keep busy pulse, waits SWBB_M7_RESPONSE_DELAY
     and transmits a 4T high pulse, a T low gap and the response byte.

   The bootloader supports mode 7 (MODE=7) with the same default values: if
   a value is changed here it must be changed in the bootloader too (M7_*).
   ___________________________________________________________________________

   Licensed under the Apache License, Version 2.0 */

#pragma once

#include <avr/pgmspace.h>

#ifndef SWBB_PIN
  #error "SWBB_MODE 7 requires SWBB_PIN"
#endif

/* Timing (microseconds) -------------------------------------------------- */

#ifndef SWBB_M7_BIT                 // Bit duration
  #define SWBB_M7_BIT              4
#endif
#ifndef SWBB_M7_PREAMBLE            // Frame preamble (high)
  #define SWBB_M7_PREAMBLE        (24 * SWBB_M7_BIT)
#endif
#ifndef SWBB_M7_PREAMBLE_MIN        // Shortest preamble rest accepted
  #define SWBB_M7_PREAMBLE_MIN    (11 * SWBB_M7_BIT)
#endif
#ifndef SWBB_M7_WINDOW              // Bytes between interrupt windows
  #define SWBB_M7_WINDOW           4
#endif
#ifndef SWBB_M7_GAP                 // Interrupt window (low)
  #define SWBB_M7_GAP             48
#endif
/* The transmitter's window lasts longer if its interrupts take longer than
   SWBB_M7_GAP (they can not be compensated): the receiver waits up to
   SWBB_M7_GAP_MAX for the next start bit */
#ifndef SWBB_M7_GAP_MAX
  #define SWBB_M7_GAP_MAX         (SWBB_M7_GAP + 64)
#endif
#ifndef SWBB_M7_IDLE                // Bus free after this time low
  #define SWBB_M7_IDLE ((11 * SWBB_M7_BIT) + SWBB_M7_GAP_MAX + 8)
#endif
#ifndef SWBB_M7_KEEP_BUSY           // Keep busy pulse (high)
  #define SWBB_M7_KEEP_BUSY        3
#endif
#ifndef SWBB_M7_RESPONSE_DELAY      // After the keep busy pulse falling edge
  #define SWBB_M7_RESPONSE_DELAY   6
#endif
#ifndef SWBB_M7_LISTEN              // Transmitter listens after each pulse
  #define SWBB_M7_LISTEN          15
#endif
#ifndef SWBB_M7_PULSE_WAIT          // Receiver waits for a keep busy pulse
  #define SWBB_M7_PULSE_WAIT      40
#endif
#ifndef SWBB_M7_ACK_HIGH            // Response preamble (high)
  #define SWBB_M7_ACK_HIGH        (4 * SWBB_M7_BIT)
#endif
/* Contention slot: random preamble extension, SWBB_M7_SLOTS steps */
#ifndef SWBB_M7_SLOTS
  #define SWBB_M7_SLOTS            8
#endif
#ifndef SWBB_M7_SLOT_STEP
  #define SWBB_M7_SLOT_STEP        4
#endif
/* Cycles between the detection of a start bit's rising edge and the
   beginning of the delay to the middle of its first data bit */
#ifndef SWBB_M7_ADVANCE
  #define SWBB_M7_ADVANCE         25
#endif

static_assert(
  (SWBB_M7_IDLE) > (10 * SWBB_M7_BIT + SWBB_M7_GAP_MAX + 4),
  "SWBB_M7_IDLE must be longer than 10 bits + SWBB_M7_GAP_MAX"
);

namespace swbb_m7 {
  typedef SoftwareBitBangPolling P;

  /* Statistics, only if SWBB_M7_STATS is defined (~30 bytes of RAM).
     Reception failures by cause: preamble too long, preamble too short, no
     start bit, stop bit high, start bit missed (interrupts longer than the
     window). Transmitter: longest time spent in interrupts in a window
     (Timer0 ticks), windows made longer by interrupts, frames given up in
     the contention slot. */
  #if defined(SWBB_M7_WINDOW_STATS) && !defined(SWBB_M7_STATS)
    #define SWBB_M7_STATS
  #endif
  #ifdef SWBB_M7_STATS
    #define SWBB_M7_STAT(X) X
    SWBB_STATE(volatile uint16_t fail_long, 0);
    SWBB_STATE(volatile uint16_t fail_short, 0);
    SWBB_STATE(volatile uint16_t fail_start, 0);
    SWBB_STATE(volatile uint16_t fail_stop, 0);
    SWBB_STATE(volatile uint16_t fail_late, 0);
    SWBB_STATE(volatile uint8_t max_ticks, 0);
    SWBB_STATE(volatile uint16_t overruns, 0);
    SWBB_STATE(volatile uint16_t collisions, 0);
    #ifdef SWBB_M7_WINDOW_STATS
      SWBB_STATE(volatile uint16_t window_hist[16], {});
    #endif
  #else
    #define SWBB_M7_STAT(X)
  #endif

  /* Interrupt window of the transmitter: pending interrupts are served and
     the bus is kept as it is for `cycles` in total (Timer0 compensation,
     resolution 64 cycles) */
  SWBB_INLINE static void window_wait(uint8_t sreg, uint32_t cycles) {
    #if SWBB_ATOMIC
      uint8_t start = TCNT0;
      SREG = sreg;
      __asm__ __volatile__ ("nop\n\tnop\n\tnop\n\tnop" ::: "memory");
      cli();
      uint8_t ticks = TCNT0 - start;
      SWBB_M7_STAT(if(ticks > max_ticks) max_ticks = ticks);
      #ifdef SWBB_M7_WINDOW_STATS
        if(ticks < 16) window_hist[ticks]++;
      #endif
      uint16_t used = ticks * (64 / 4);
      uint16_t loops = (uint16_t)((cycles - 12) / 4);
      if(used >= loops) SWBB_M7_STAT(overruns++);
      P::delay_loops((used < loops) ? (loops - used) : 1);
    #else
      (void)sreg;
      P::delay_loops((uint16_t)(cycles / 4));
    #endif
  }

  /* Interrupt window of the receiver (it must listen again before the
     transmitter's window ends) */
  SWBB_INLINE static void window_serve(uint8_t sreg) {
    #if SWBB_ATOMIC
      SREG = sreg;
      __asm__ __volatile__ ("nop\n\tnop\n\tnop\n\tnop" ::: "memory");
      cli();
    #else
      (void)sreg;
    #endif
  }

  /* Transmits start bit, 8 data bits (LSB first) and stop bit, each one of
     exactly T: 12 cycles + 4 * loops + nops per bit (the bus is driven) */
  static void tx_byte(uint8_t b) {
    uint16_t r = ((uint16_t)b << 1) | 1, n;
    uint8_t i = 10;
    __asm__ __volatile__ (
      "1: sbrc %A[r], 0            \n\t"
      "   sbi  %[port], %[bit]     \n\t"
      "   sbrs %A[r], 0            \n\t"
      "   cbi  %[port], %[bit]     \n\t"
      "   lsr  %B[r]               \n\t"
      "   ror  %A[r]               \n\t"
      "   ldi  %A[n], lo8(%[loops]) \n\t"
      "   ldi  %B[n], hi8(%[loops]) \n\t"
      "2: sbiw %[n], 1             \n\t"
      "   brne 2b                  \n\t"
      "   .rept %[nops]            \n\t"
      "   nop                      \n\t"
      "   .endr                    \n\t"
      "   dec  %[i]                \n\t"
      "   brne 1b                  \n\t"
      : [r] "+r" (r), [i] "+r" (i), [n] "=&w" (n)
      : [port] "I" (_SFR_IO_ADDR(SWBB_PORT)), [bit] "I" (SWBB_BIT),
        [loops] "i" (SWBB_BIT_LOOPS(SWBB_M7_BIT, 12)),
        [nops] "i" (SWBB_BIT_NOPS(SWBB_M7_BIT, 12))
    );
  }

  /* Waits for the rising edge of a start bit (up to `loops` iterations of 6
     cycles) and receives the byte. Returns the byte, or a value > 255 if
     the start bit did not come or the stop bit is not low. */
  static uint16_t rx_byte(uint16_t loops) {
    if(!P::wait_while_low(loops)) return 0x100;
    __builtin_avr_delay_cycles(
      SWBB_CYCLES(SWBB_M7_BIT * 1.5) - SWBB_M7_ADVANCE
    );
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
      : [in] "I" (_SFR_IO_ADDR(SWBB_IN)), [bit] "I" (SWBB_BIT),
        [loops] "i" (SWBB_BIT_LOOPS(SWBB_M7_BIT, 8)),
        [nops] "i" (SWBB_BIT_NOPS(SWBB_M7_BIT, 8))
    );
    // Middle of the stop bit: it must be low
    __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_M7_BIT) - 4);
    if(P::bus_high()) return 0x200;
    return value;
  }

  /* Receives a frame (call it with interrupts disabled, while the bus is
     high). If `check_preamble` at least SWBB_M7_PREAMBLE_MIN of the high
     level must be left. Returns the frame length or 0. */
  static uint16_t rx_frame(
    uint8_t *data, uint16_t max_length, bool check_preamble, uint8_t sreg,
    uint8_t id = 0, bool only_id = false
  ) {
    const uint16_t max_pre = SWBB_LOOPS(
      SWBB_M7_PREAMBLE + (SWBB_M7_SLOTS * SWBB_M7_SLOT_STEP) + 20
    );
    uint16_t left = P::wait_while_high(max_pre);
    if(!left) { SWBB_M7_STAT(fail_long++); return 0; }
    if(check_preamble && (max_pre - left) < SWBB_LOOPS(SWBB_M7_PREAMBLE_MIN)) {
      SWBB_M7_STAT(fail_short++);
      return 0;
    }
    uint16_t length = max_length;
    // First start bit: after the T low gap
    uint16_t wait = SWBB_LOOPS(2 * SWBB_M7_BIT);
    for(uint16_t i = 0; i < length; i++) {
      uint16_t b = rx_byte(wait);
      if(b > 255) {
        if(b == 0x100) SWBB_M7_STAT(fail_start++);
        else SWBB_M7_STAT(fail_stop++);
        return 0;
      }
      data[i] = b;
      if(i == 0) {
        if(only_id && (b != id) && (b != PJON_BROADCAST)) return 0;
      } else if(i == 1) {
        if(b & PJON_EXT_LEN_BIT) length = max_length; // known at byte 3
      } else if((i == 2) && !(data[1] & PJON_EXT_LEN_BIT)) {
        length = b;
        if((length < 5) || (length > max_length)) return 0;
      } else if((i == 3) && (data[1] & PJON_EXT_LEN_BIT)) {
        length = ((uint16_t)data[2] << 8) | b;
        if((length < 6) || (length > max_length)) return 0;
      }
      // Next start bit: half a stop bit later, or after the window
      if(((i + 1) % SWBB_M7_WINDOW) == 0) {
        window_serve(sreg);
        /* If this device's interrupts took longer than the window the start
           bit was missed: its timing is unknown, give up */
        if(P::bus_high()) { SWBB_M7_STAT(fail_late++); return 0; }
        wait = SWBB_LOOPS(SWBB_M7_BIT + SWBB_M7_GAP_MAX + 8);
      } else wait = SWBB_LOOPS((2 * SWBB_M7_BIT) + 2);
    }
    return length;
  }

  /* Transmits a frame (interrupts disabled). Contention slot: the preamble
     is extended by `slot` (0 to SWBB_M7_SLOTS - 1) steps of
     SWBB_M7_SLOT_STEP, then the bus is driven low (discharged), released and
     read: if it is high another device started transmitting at the same
     time with a longer preamble, this one gives up at once (returns false)
     and the other frame is not disturbed. */
  static bool tx_frame(
    const uint8_t *data, uint16_t length, uint8_t sreg, uint8_t slot
  ) {
    P::bus_drive_high();
    window_wait(sreg, SWBB_CYCLES(SWBB_M7_PREAMBLE));
    for(uint8_t s = 0; s < slot; s++)
      __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_M7_SLOT_STEP) - 4);
    P::bus_clear();                       // discharge the bus
    __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_DISCHARGE_US));
    SWBB_DDR &= ~(1 << SWBB_BIT);         // release
    __builtin_avr_delay_cycles(SWBB_CYCLES(1));
    if(P::bus_high()) { SWBB_M7_STAT(collisions++); return false; }
    SWBB_DDR |= (1 << SWBB_BIT);          // low again, the T low gap
    // The check took SWBB_DISCHARGE_US + 1us of it (or more if T is short)
    __builtin_avr_delay_cycles(
      (SWBB_CYCLES(SWBB_M7_BIT) > SWBB_CYCLES(SWBB_DISCHARGE_US + 1) + 12) ?
      (SWBB_CYCLES(SWBB_M7_BIT) - SWBB_CYCLES(SWBB_DISCHARGE_US + 1) - 12) : 0
    );
    for(uint16_t i = 0; i < length; i++) {
      tx_byte(data[i]);
      if((((i + 1) % SWBB_M7_WINDOW) == 0) && (i + 1 < length))
        window_wait(sreg, SWBB_CYCLES(SWBB_M7_GAP));
    }
    P::bus_release();
    return true;
  }

  /* Response (receiver side): synchronize to a keep busy pulse and answer */
  static void tx_response(uint8_t response) {
    P::bus_release();
    P::wait_while_low(SWBB_LOOPS(SWBB_M7_PULSE_WAIT));
    P::wait_while_high(SWBB_LOOPS(SWBB_M7_KEEP_BUSY + 6));
    __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_M7_RESPONSE_DELAY));
    P::bus_drive_high();
    __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_M7_ACK_HIGH));
    P::bus_clear();
    __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_M7_BIT) - 8);
    tx_byte(response);
    P::bus_release();
  }

  /* CRC32 (IEEE 802.3) with a 16 entries table (~6us per byte at 16MHz) */
  static const uint32_t crc32_table[16] PROGMEM = {
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
    0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
    0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
    0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
  };

  static inline uint32_t crc32_update(uint32_t c, uint8_t b) {
    c ^= b;
    c = (c >> 4) ^ pgm_read_dword(&crc32_table[c & 15]);
    c = (c >> 4) ^ pgm_read_dword(&crc32_table[c & 15]);
    return c;
  }

  /* True if both CRCs of the frame are correct (wake receiver) */
  __attribute__((unused))
  static bool frame_valid(const uint8_t *d, uint16_t length) {
    uint8_t ext = (d[1] & PJON_EXT_LEN_BIT) ? 1 : 0;
    if(PJON_crc8::compute(d, 3 + ext) != d[3 + ext]) return false;
    if(d[1] & PJON_CRC_BIT) {
      uint32_t c = 0xFFFFFFFF;
      for(uint16_t i = 0; i < length - 4; i++) c = crc32_update(c, d[i]);
      c = ~c;
      for(uint16_t i = length - 4; i < length; i++, c <<= 8)
        if(d[i] != (uint8_t)(c >> 24)) return false;
      return true;
    }
    return PJON_crc8::compute(d, length - 1) == d[length - 1];
  }
}

/* Polling (bus.receive) and interrupt triggered (SWBB_INTERRUPT_WAKE)
   implementations share the frame code above */

#ifdef SWBB_INTERRUPT_WAKE
  #ifndef SWBB_EXT_INT_VECT
    #error "SWBB_INTERRUPT_WAKE requires a SWBB_PIN with an external interrupt (2 or 3; on the Mega also 18-21)"
  #endif
  #define SWBB_M7_WAKE_VECT   SWBB_EXT_INT_VECT
  #define SWBB_M7_WAKE_ENABLE SWBB_EXT_INT_ENABLE
  #define SWBB_M7_WAKE_FLAG   SWBB_EXT_INT_FLAG
  #define SWBB_M7_WAKE_SENSE  SWBB_EXT_INT_SENSE_RISING
  #ifndef SWBB_WAKE_FRAMES
    #define SWBB_WAKE_FRAMES 2
  #endif
  /* Rising edges closer than this to the previous one are inside a frame or
     an exchange (byte starts every 10T + window, keep busy pulses) */
  #ifndef SWBB_M7_QUIET
    #define SWBB_M7_QUIET ((10 * SWBB_M7_BIT) + SWBB_M7_GAP_MAX + 4)
  #endif

  namespace swbb_m7 {
    struct Frame {
      uint8_t  data[PJON_PACKET_MAX_LENGTH];
      uint16_t length;
    };
    SWBB_STATE(Frame queue[SWBB_WAKE_FRAMES], {});
    SWBB_STATE(volatile uint8_t head, 0);
    SWBB_STATE(volatile uint8_t count, 0);
    SWBB_STATE(uint8_t id, 0);
    SWBB_STATE(uint32_t last_rise, 0);
    #ifdef SWBB_M7_STATS
      SWBB_STATE(volatile uint16_t wakes, 0);
      SWBB_STATE(volatile uint16_t stored, 0);
      SWBB_STATE(volatile uint16_t failed, 0);
      SWBB_STATE(volatile uint16_t dropped, 0);
    #endif

    static void wake_receive() {
      SWBB_M7_STAT(wakes++);
      if(count >= SWBB_WAKE_FRAMES) { SWBB_M7_STAT(dropped++); return; }
      Frame &f = queue[(uint8_t)(head + count) % SWBB_WAKE_FRAMES];
      // Interrupts are enabled in the windows (nested), INT0 is masked
      uint16_t length = rx_frame(
        f.data, PJON_PACKET_MAX_LENGTH, false, 0x80, id, true
      );
      if(!length) { SWBB_M7_STAT(failed++); return; }
      sei(); // CRC check with interrupts enabled
      bool valid = frame_valid(f.data, length);
      cli();
      // Not valid: not acknowledged, the transmitter retries
      if(!valid) { SWBB_M7_STAT(failed++); return; }
      /* Acknowledged here (PJON reads the frame later): this device id,
         requested, local mode (shared mode and router mode not supported) */
      if(
        (f.data[0] == id) && (f.data[1] & PJON_ACK_REQ_BIT) &&
        !(f.data[1] & PJON_MODE_BIT)
      ) tx_response(PJON_ACK);
      f.length = length;
      count++;
      SWBB_M7_STAT(stored++);
    }
  }
#endif

class SoftwareBitBang : public SoftwareBitBangPolling {
  public:
    typedef SoftwareBitBangPolling P;

    bool begin(uint8_t did = 0) {
      P::begin(did);
      #ifdef SWBB_INTERRUPT_WAKE
        swbb_m7::id = did;
        SWBB_EXT_INT_EICR |= SWBB_M7_WAKE_SENSE;
        irq_on();
      #endif
      return true;
    };


    /* The bus is free if it stays low for SWBB_M7_IDLE plus a random time */

    bool can_start() {
      P::bus_release();
      if(P::wait_while_low(SWBB_LOOPS(SWBB_M7_IDLE + SWBB_LATENCY)))
        return false;
      uint8_t random_delay = PJON_RANDOM(SWBB_COLLISION_DELAY);
      if(P::wait_while_low(
        ((uint16_t)random_delay * (F_CPU / 1000000UL)) / 6 + 1
      )) return false;
      return true;
    };


    /* The whole frame is received (or served from the queue) at once */

    uint16_t receive_frame(uint8_t *data, uint16_t max_length) {
      if(max_length != PJON_PACKET_MAX_LENGTH) return SWBB_FAIL;
      #ifdef SWBB_INTERRUPT_WAKE
        using namespace swbb_m7;
        if(!count) return SWBB_FAIL;
        Frame &f = queue[head];
        uint16_t length = f.length;
        for(uint16_t i = 0; i < length; i++) data[i] = f.data[i];
        uint8_t sreg = SREG;
        cli();
        head = (uint8_t)(head + 1) % SWBB_WAKE_FRAMES;
        count--;
        SREG = sreg;
        return length;
      #else
        if(!P::bus_high()) return SWBB_FAIL;
        uint8_t sreg = P::atomic_begin();
        #ifdef SWBB_M7_DEBUG
          DDRD |= 8; PORTD |= 8;
        #endif
        uint16_t length = swbb_m7::rx_frame(data, max_length, true, sreg);
        #ifdef SWBB_M7_DEBUG
          PORTD &= ~8;
        #endif
        P::atomic_end(sreg);
        return length ? length : SWBB_FAIL;
      #endif
    };


    uint16_t receive_byte() {
      return SWBB_FAIL;
    };


    void send_frame(uint8_t *data, uint16_t length) {
      irq_off();
      _timeout = (length * SWBB_RESPONSE_OFFSET) + SWBB_LATENCY;
      uint8_t slot = PJON_RANDOM(SWBB_M7_SLOTS);
      uint8_t sreg = P::atomic_begin();
      _collision = !swbb_m7::tx_frame(data, length, sreg, slot);
      P::atomic_end(sreg);
      irq_on();
    };


    /* Keep the bus busy and listen for the response */

    uint16_t receive_response() {
      irq_off();
      uint16_t response = SWBB_FAIL;
      if(_collision) { // Another frame is being transmitted: do not disturb
        irq_on();
        return SWBB_FAIL;
      }
      uint32_t time = PJON_MICROS();
      while((uint32_t)(PJON_MICROS() - time) < _timeout) {
        uint8_t sreg = P::atomic_begin();
        if(!P::bus_high()) {
          P::bus_drive_high();
          __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_M7_KEEP_BUSY));
          P::bus_release();
          P::wait_while_high(SWBB_LOOPS(SWBB_RELEASE_FALL));
          P::wait_while_low(SWBB_LOOPS(SWBB_M7_LISTEN));
        }
        if(P::bus_high()) {
          uint16_t max = SWBB_LOOPS(SWBB_M7_ACK_HIGH + 8);
          uint16_t left = P::wait_while_high(max);
          if(
            left && ((uint16_t)(max - left) >=
              SWBB_LOOPS(SWBB_M7_ACK_HIGH / 2))
          ) {
            uint16_t b = swbb_m7::rx_byte(SWBB_LOOPS(2 * SWBB_M7_BIT));
            if(b < 256) response = b;
          }
          P::atomic_end(sreg);
          break; // A response was detected: stop keeping the bus busy
        }
        P::atomic_end(sreg);
      }
      irq_on();
      return response;
    };


    void send_response(uint8_t response) {
      #ifdef SWBB_INTERRUPT_WAKE
        (void)response; // Sent by the interrupt
      #else
        uint8_t sreg = P::atomic_begin();
        swbb_m7::tx_response(response);
        P::atomic_end(sreg);
      #endif
    };


    static void irq_off() {
      #ifdef SWBB_INTERRUPT_WAKE
        EIMSK &= ~SWBB_M7_WAKE_ENABLE;
      #endif
    };


    static void irq_on() {
      #ifdef SWBB_INTERRUPT_WAKE
        uint8_t sreg = SREG;
        cli();
        swbb_m7::last_rise = PJON_MICROS();
        EIFR = SWBB_M7_WAKE_FLAG;
        EIMSK |= SWBB_M7_WAKE_ENABLE;
        SREG = sreg;
      #endif
    };

  private:
    uint16_t _timeout;
    bool _collision = false;
};

#if defined(SWBB_INTERRUPT_WAKE) && !defined(SWBB_STATE_EXTERN)
ISR(SWBB_M7_WAKE_VECT) {
  using namespace swbb_m7;
  uint32_t now = PJON_MICROS();
  uint32_t gap = now - last_rise;
  last_rise = now;
  if(gap >= SWBB_M7_QUIET) { // Not inside a frame or an exchange
    EIMSK &= ~SWBB_M7_WAKE_ENABLE; // No re-entry while nested
    SWBB_FRAME_BEGIN();
    wake_receive();
    SWBB_FRAME_END();
    last_rise = PJON_MICROS();
    EIFR = SWBB_M7_WAKE_FLAG;
    EIMSK |= SWBB_M7_WAKE_ENABLE;
  }
}
#endif
