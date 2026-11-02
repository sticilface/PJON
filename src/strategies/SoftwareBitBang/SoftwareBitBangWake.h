/* SoftwareBitBang interrupt triggered receiver (prototype)
   ___________________________________________________________________________

   Selected defining SWBB_INTERRUPT_WAKE and SWBB_PIN 2 (INT0) or 3 (INT1)
   on the ATmega88/168/328 before including PJON:
     #define SWBB_PIN 2
     #define SWBB_INTERRUPT_WAKE
     #include <PJONSoftwareBitBang.h>

   The rising edge of the first padding bit of a frame triggers INT0/INT1.
   The interrupt masks itself and receives the whole frame with the cycle
   exact direct port access code (SoftwareBitBang.h, SWBB_PIN): interrupts
   are disabled while each byte is received and enabled (nested in this
   interrupt) while the received byte is processed, during the following
   padding bit. After the last byte the CRCs are checked (~6us per byte,
   interrupts enabled) and, if the frame is addressed to this device and
   requests it, the synchronous acknowledgement is transmitted by the
   interrupt, well within the transmitter's response timeout.
   The frame is stored in a queue of SWBB_WAKE_FRAMES frames (default 2) that
   bus.receive() serves to PJON: frames are received while the application
   is busy and bus.receive() can be called at any time.

   - Rising edges that follow another one by less than SWBB_WAKE_QUIET
     (a padding bit and 9.5 data bits: the longest interval between two
     rising edges inside a frame, shorter than the time any transmitter
     waits with the bus low before starting a frame) are ignored in ~5us:
     the remaining bytes of a frame addressed to another device (abandoned
     after its first byte), keep busy bits, responses.
   - The first padding bit is accepted whatever its measured duration (the
     interrupt may have been delayed by another one), the following two are
     fully checked.
   - Other interrupt handlers up to ~25us (mode 3, 5, 6) or ~40us (mode 1)
     long, every 256us, do not disturb the reception (measured).
   - Acknowledged by the interrupt: frames with this device id (passed to
     begin), acknowledgement requested, correct CRCs, that PJON::receive()
     would acknowledge (same header checks, see SoftwareBitBangAccept.h),
     with a free place in the queue, if bus.receive() was called in the last
     SWBB_WAKE_ALIVE_MS. Not supported: shared mode acknowledgements, MAC
     addresses, router mode (acknowledging other device ids): such frames
     are stored without acknowledgement. Frames PJON would drop are not
     stored. Frames not acknowledged by the interrupt are not acknowledged
     later.
   - If the queue is full new frames are ignored (not acknowledged, the
     transmitter retries).
   - Timer1 is not used. Timer0 is read (as by SWBB_PIN).
   - While a frame is received the application does not run (as when it is
     received calling bus.receive()), other interrupts are served between
     bytes.
   ___________________________________________________________________________

   Licensed under the Apache License, Version 2.0 */

#pragma once

#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#ifndef SWBB_EXT_INT_VECT
  #error "SWBB_INTERRUPT_WAKE requires a SWBB_PIN with an external interrupt (2 or 3; on the Mega also 18-21)"
#endif
#define SWBB_WAKE_VECT   SWBB_EXT_INT_VECT
#define SWBB_WAKE_ENABLE SWBB_EXT_INT_ENABLE
#define SWBB_WAKE_FLAG   SWBB_EXT_INT_FLAG
#define SWBB_WAKE_SENSE  SWBB_EXT_INT_SENSE_RISING
#define SWBB_WAKE_SENSE_MASK SWBB_EXT_INT_SENSE_MASK

#ifndef SWBB_WAKE_FRAMES
  #define SWBB_WAKE_FRAMES 2
#endif

/* The interrupt acknowledges a frame only if bus.receive() was called in
   the last SWBB_WAKE_ALIVE_MS milliseconds (0: always): if the application
   is stuck (or no longer calls bus.receive) its device stops acknowledging
   and the transmitters see the failure, instead of being told that packets
   are delivered. It must be longer than the longest time the application
   spends between two bus.receive() calls. */
#ifndef SWBB_WAKE_ALIVE_MS
  #define SWBB_WAKE_ALIVE_MS 100
#endif

#include "SoftwareBitBangAccept.h"

#ifndef SWBB_WAKE_QUIET
  #define SWBB_WAKE_QUIET \
    ((uint32_t)(SWBB_WIRE_PAD + (SWBB_WIRE_BIT * 9.5)))
#endif

#ifdef SWBB_WAKE_STATS
  #define SWBB_WAKE_STAT(X) X
#else
  #define SWBB_WAKE_STAT(X)
#endif

namespace swbb_wake {
  typedef SoftwareBitBangPolling P;

  struct Frame {
    uint8_t  data[PJON_PACKET_MAX_LENGTH];
    uint8_t  length;
    uint8_t  acked;
  };

  /* State shared by the interrupt and the strategy. In a project of several
     source files that include PJON, define SWBB_WAKE_SHARED for all of them
     and SWBB_WAKE_IMPLEMENTATION in exactly one (before any include): only
     that one defines the state and the interrupt handler, the others see the
     same state. Without SWBB_WAKE_SHARED (one source file, e.g. a sketch)
     every file gets its own copy, as before. */
#if defined(SWBB_WAKE_SHARED) && !defined(SWBB_WAKE_IMPLEMENTATION)
  extern Frame queue[SWBB_WAKE_FRAMES];
  extern volatile uint8_t head, count;
  extern uint8_t id;
  extern uint8_t end;
  extern uint32_t last_rise;
  extern volatile uint32_t last_alive;
  #ifdef SWBB_WAKE_STATS
    extern volatile uint16_t wakes, stored, foreign, dropped, rejected, unalive;
    extern volatile uint16_t failed_first, failed_init, failed_byte;
  #endif
#else
  #ifdef SWBB_WAKE_SHARED
    #define SWBB_WAKE_STORAGE
  #else
    #define SWBB_WAKE_STORAGE static
  #endif
  SWBB_WAKE_STORAGE Frame queue[SWBB_WAKE_FRAMES];
  SWBB_WAKE_STORAGE volatile uint8_t head = 0, count = 0;
  SWBB_WAKE_STORAGE uint8_t id = 0;
  SWBB_WAKE_STORAGE uint8_t end = 0;          // Timer0 count at the end of a byte
  SWBB_WAKE_STORAGE uint32_t last_rise = 0;   // micros() of the last rising edge
  SWBB_WAKE_STORAGE volatile uint32_t last_alive = 0; // millis() of the last bus.receive()

  // Statistics (define SWBB_WAKE_STATS to count, 18 bytes of RAM)
  #ifdef SWBB_WAKE_STATS
    SWBB_WAKE_STORAGE volatile uint16_t wakes = 0, stored = 0, foreign = 0, dropped = 0;
    SWBB_WAKE_STORAGE volatile uint16_t rejected = 0, unalive = 0;
    SWBB_WAKE_STORAGE volatile uint16_t failed_first = 0, failed_init = 0, failed_byte = 0;
  #endif
#endif

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

  /* First padding bit: the interrupt was triggered by its rising edge, its
     measured duration is not checked (the interrupt may have been delayed) */
  static bool sync_first() {
    if(!P::bus_high()) return false;
    if(!P::wait_while_high(SWBB_LOOPS(SWBB_WIRE_PAD_MAX * SWBB_MAX_PREAMBLE)))
      return false;
    __builtin_avr_delay_cycles(
      SWBB_CYCLES(SWBB_RX_BIT / 2) - (SWBB_SAMPLE_ADVANCE / 2)
    );
    if(P::bus_high()) return false;
    __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_RX_BIT / 2));
    end = TCNT0;
    return true;
  }

  /* Following padding bits, measured from the end of the previous byte or
     pad (as SoftwareBitBang::sync with continued = true) */
  static bool sync_next() {
    uint16_t late = 0, max_high = SWBB_LOOPS(SWBB_WIRE_PAD_MAX);
    if(P::bus_high()) late = (uint8_t)(TCNT0 - end) * (64 / 6);
    else if(!P::wait_while_low(SWBB_LOOPS(SWBB_RX_BIT))) return false;
    uint16_t left = P::wait_while_high(max_high);
    if(!left) return false;
    if((uint16_t)(max_high - left + late) < SWBB_LOOPS(SWBB_ACCEPTANCE))
      return false;
    __builtin_avr_delay_cycles(
      SWBB_CYCLES(SWBB_RX_BIT / 2) - (SWBB_SAMPLE_ADVANCE / 2)
    );
    if(P::bus_high()) return false;
    __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_RX_BIT / 2));
    end = TCNT0;
    return true;
  }

  /* Interrupts enabled (nested) while the padding bit is transmitted */
  static inline void window() {
    sei();
    __asm__ __volatile__ ("nop\n\tnop\n\tnop\n\tnop" ::: "memory");
    cli();
  }

  /* Synchronous acknowledgement (interrupts disabled, served during the
     padding bits) */
  static void acknowledge() {
    P::bus_release();
    P::wait_while_low(SWBB_LOOPS(SWBB_RESPONSE_PULSE_WAIT));
    P::wait_while_high(SWBB_LOOPS(SWBB_WIRE_BIT / 2));
    __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_RESPONSE_DELAY));
    P::bus_drive_high();
    P::send_pad(0x80);
    P::send_byte(PJON_ACK, 0x80);
    P::bus_release();
  }

  /* Receives a frame, called by the interrupt with interrupts disabled.
     Inlined in the interrupt: one call frame less on the stack, that the
     interrupt adds to the application's (measured: see stack_eval.sh) */
  static inline __attribute__((always_inline)) void receive() {
    SWBB_WAKE_STAT(wakes++);
    if(count >= SWBB_WAKE_FRAMES) { SWBB_WAKE_STAT(dropped++); return; }
    Frame &f = queue[(uint8_t)(head + count) % SWBB_WAKE_FRAMES];
    if(!sync_first()) { SWBB_WAKE_STAT(failed_first++); return; }
    window();
    if(!sync_next()) { SWBB_WAKE_STAT(failed_init++); return; }
    window();
    if(!sync_next()) { SWBB_WAKE_STAT(failed_init++); return; }
    /* Between bytes only the byte is stored and the first bytes checked,
       so that other interrupts have most of the padding bit to run (two
       interrupt handlers can be served in the same padding bit) */
    uint8_t length = PJON_PACKET_MAX_LENGTH;
    for(uint8_t i = 0; i < length; i++) {
      if(!sync_next()) { SWBB_WAKE_STAT(failed_byte++); return; }
      uint8_t b = P::read_byte();
      end = TCNT0;
      sei(); // The next padding bit is being received
      f.data[i] = b;
      if(i == 0) {
        if((b != id) && (b != PJON_BROADCAST)) {
          SWBB_WAKE_STAT(foreign++);
          cli();
          return; // Not for this device, its other bytes are ignored
        }
      } else if(i == 1) {
        if(b & PJON_EXT_LEN_BIT) { cli(); return; }
      } else if(i == 2) {
        length = b;
        if((length < 5) || (length > PJON_PACKET_MAX_LENGTH)) {
          cli();
          return;
        }
      }
      cli();
    }
    /* Error detection (interrupts enabled): ~6us per byte, done before the
       transmitter's response timeout (20us per byte) */
    sei();
    uint8_t header = f.data[1];
    bool valid = PJON_crc8::compute(f.data, 3) == f.data[3];
    if(valid) {
      if(header & PJON_CRC_BIT) {
        uint32_t c = 0xFFFFFFFF;
        for(uint8_t i = 0; i < length - 4; i++) c = crc32_update(c, f.data[i]);
        c = ~c;
        for(uint8_t i = length - 4; i < length; i++, c <<= 8)
          if(f.data[i] != (uint8_t)(c >> 24)) valid = false;
      } else if(PJON_crc8::compute(f.data, length - 1) != f.data[length - 1])
        valid = false;
    }
    /* The same checks PJON::receive() does before acknowledging (see
       SoftwareBitBangAccept.h): a frame PJON would drop is not acknowledged
       and not stored */
    uint8_t accept = valid ? swbb_accept(f.data, length) : SWBB_REJECT;
    bool alive = !SWBB_WAKE_ALIVE_MS ||
      ((uint32_t)(millis() - last_alive) <= SWBB_WAKE_ALIVE_MS);
    cli();
    if(accept == SWBB_REJECT) { SWBB_WAKE_STAT(rejected++); return; }
    bool requested = (f.data[0] == id) && (header & PJON_ACK_REQ_BIT);
    bool ack = requested && (accept == SWBB_ACCEPT);
    if(ack && !alive) {
      /* Not acknowledged: the transmitter retries, storing it would deliver
         it twice */
      SWBB_WAKE_STAT(unalive++);
      return;
    }
    if(ack) acknowledge();
    f.length = length;
    f.acked = ack;
    count++;
    SWBB_WAKE_STAT(stored++);
  }
}

class SoftwareBitBang : public SoftwareBitBangPolling {
  public:
    bool begin(uint8_t did = 0) {
      SoftwareBitBangPolling::begin(did);
      swbb_wake::id = did;
      swbb_wake::last_alive = millis();
      #ifdef SWBB_IRQ_PIN_DEBUG // PD3 high while the interrupt runs
        DDRD |= 8;
      #endif
      SWBB_EXT_INT_EICR =
        (SWBB_EXT_INT_EICR & ~SWBB_WAKE_SENSE_MASK) | SWBB_WAKE_SENSE;
      irq_on();
      return true;
    };


    /* Serve a frame received by the interrupt (all its bytes at once) */

    uint16_t receive_frame(uint8_t *data, uint16_t max_length) {
      using namespace swbb_wake;
      if(max_length != PJON_PACKET_MAX_LENGTH) return SWBB_FAIL;
      { // The application calls bus.receive() (atomic: read by the interrupt)
        uint32_t now = millis();
        uint8_t sreg = SREG;
        cli();
        last_alive = now;
        SREG = sreg;
      }
      if(!count) return SWBB_FAIL;
      Frame &f = queue[head];
      uint16_t length = f.length;
      if(length > max_length) length = max_length;
      for(uint16_t i = 0; i < length; i++) data[i] = f.data[i];
      _acked = f.acked;
      uint8_t sreg = SREG;
      cli();
      head = (uint8_t)(head + 1) % SWBB_WAKE_FRAMES;
      count--;
      SREG = sreg;
      return length;
    };


    uint16_t receive_byte() {
      return SWBB_FAIL;
    };


    void send_frame(uint8_t *data, uint16_t length) {
      irq_off();
      SoftwareBitBangPolling::send_frame(data, length);
      irq_on();
    };


    uint16_t receive_response() {
      irq_off();
      uint16_t response = SoftwareBitBangPolling::receive_response();
      irq_on();
      return response;
    };


    /* The acknowledgement is sent by the interrupt, when PJON reads the
       frame it is too late to send it */

    void send_response(uint8_t response) {
      (void)response;
      _acked = false;
    };


    static void irq_off() {
      EIMSK &= ~SWBB_WAKE_ENABLE;
    };


    static void irq_on() {
      uint8_t sreg = SREG;
      cli();
      swbb_wake::last_rise = PJON_MICROS();
      EIFR = SWBB_WAKE_FLAG;
      EIMSK |= SWBB_WAKE_ENABLE;
      SREG = sreg;
    };

  private:
    bool _acked = false;
};

#if !defined(SWBB_WAKE_SHARED) || defined(SWBB_WAKE_IMPLEMENTATION)
ISR(SWBB_WAKE_VECT) {
  using namespace swbb_wake;
  #ifdef SWBB_IRQ_PIN_DEBUG // PD3 high while the interrupt runs
    PORTD |= 8;
  #endif
  uint32_t now = PJON_MICROS();
  uint32_t gap = now - last_rise;
  last_rise = now;
  if(gap >= SWBB_WAKE_QUIET) { // Not inside a frame or an exchange
    EIMSK &= ~SWBB_WAKE_ENABLE; // No re-entry while nested
    SWBB_FRAME_BEGIN();
    receive();
    SWBB_FRAME_END();
    last_rise = PJON_MICROS();
    EIFR = SWBB_WAKE_FLAG;
    EIMSK |= SWBB_WAKE_ENABLE;
  }
  #ifdef SWBB_IRQ_PIN_DEBUG
    PORTD &= ~8;
  #endif
}
#endif
