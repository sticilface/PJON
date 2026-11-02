/* SoftwareBitBang interrupt driven receiver (prototype)
   ___________________________________________________________________________

   Selected defining SWBB_INTERRUPT and SWBB_PIN 2 (INT0) or 3 (INT1) on the
   ATmega88/168/328 before including PJON:
     #define SWBB_PIN 2
     #define SWBB_INTERRUPT
     #include <PJONSoftwareBitBang.h>

   Reception: every edge of the bus triggers INT0/INT1, the edge is time
   stamped with Timer1 (free running at F_CPU / 8, 0.5us at 16MHz) and a
   state machine decodes the frame initializer, the synchronization pads and
   the data bits from the edge times. A Timer1 compare match interrupt ends
   each byte 9.5 bits after its synchronization (the last bits of a byte may
   produce no edge). Decoded bytes are stored in a frame buffer that
   receive_frame serves to PJON: frames are received while the application is
   busy, PJON does not need to poll the bus.

   Transmission, synchronous response and medium access use the direct port
   access implementation (SoftwareBitBang.h, SWBB_PIN), with the edge
   interrupt disabled.

   Synchronous acknowledgement:
   - default: sent by PJON when bus.receive() reads the frame, so
     bus.receive() must be called before the transmitter's response timeout
     (SWBB_RESPONSE_OFFSET * length + SWBB_LATENCY microseconds after the
     end of the frame, ~500us for a 25 bytes frame).
   - SWBB_INTERRUPT_ACK: sent by the interrupt as soon as the last byte is
     received, if the frame is addressed to this device id (the one passed to
     begin), it requests an acknowledgement, its CRCs are correct and it is a
     local mode frame without extended length. The frame stays in the buffer
     until bus.receive() reads it; while the buffer is occupied new frames
     are ignored and not acknowledged (the transmitter retries). Not suited
     for routers and shared mode (no bus id check).

   Resources taken from the application: Timer1 (reconfigured as a free
   running timer: no analogWrite on pins 9 and 10, no Servo library), INT0
   (or INT1). Interrupts that run when an edge occurs delay its time stamp:
   with mode 3 a delay longer than ~14us corrupts the byte (AVR interrupts
   are not nested).
   ___________________________________________________________________________

   Licensed under the Apache License, Version 2.0 */

#pragma once

#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#ifndef SWBB_EXT_INT_VECT
  #error "SWBB_INTERRUPT requires a SWBB_PIN with an external interrupt (2 or 3; on the Mega also 18-21)"
#endif
#define SWBB_IRQ_VECT   SWBB_EXT_INT_VECT
#define SWBB_IRQ_ENABLE SWBB_EXT_INT_ENABLE
#define SWBB_IRQ_FLAG   SWBB_EXT_INT_FLAG
#define SWBB_IRQ_SENSE  SWBB_EXT_INT_SENSE_CHANGE
#define SWBB_IRQ_SENSE_MASK SWBB_EXT_INT_SENSE_MASK

// Microseconds to Timer1 ticks (F_CPU / 8)
#define SWBB_T1(US) ((uint16_t)((US) * (F_CPU / 8000000.0)))
#define SWBB_T1_BIT SWBB_T1(SWBB_RX_BIT)

namespace swbb_irq {
  enum { IDLE, INIT, DATA, PAD_LOW, PAD_HIGH, IGNORE };

  SWBB_STATE(volatile uint8_t buf[PJON_PACKET_MAX_LENGTH], {});
  SWBB_STATE(volatile uint16_t wr, 0);        // bytes written in buf
  SWBB_STATE(volatile uint8_t filling, 0);   // the interrupt is writing buf
  SWBB_STATE(volatile uint8_t ready, 0);     // buf holds a frame not consumed
  SWBB_STATE(volatile uint8_t acked, 0);     // acknowledged by the interrupt
  SWBB_STATE(volatile uint16_t frames, 0);    // statistics: frames started
  SWBB_STATE(volatile uint16_t ignored, 0);   // frames ignored (buffer occupied)
  SWBB_STATE(volatile uint32_t end_us, 0);    // micros() at the end of the frame

  SWBB_STATE(uint8_t state, IDLE);
  SWBB_STATE(uint8_t level, 0);
  SWBB_STATE(uint8_t pads, 0);
  SWBB_STATE(uint8_t pos, 0);
  SWBB_STATE(uint16_t sync, 0);
  SWBB_STATE(uint16_t high_start, 0);
  SWBB_STATE(uint16_t bits, 0);

  #ifdef SWBB_INTERRUPT_ACK
    SWBB_STATE(uint8_t id, 0);
    SWBB_STATE(uint8_t ack, 0);
    SWBB_STATE(uint8_t header, 0);
    SWBB_STATE(uint8_t crc8, 0);
    SWBB_STATE(uint16_t length, 0);
    SWBB_STATE(uint32_t crc32, 0);
  #endif

  #ifdef SWBB_INTERRUPT_ACK
    /* CRC32 (IEEE 802.3) with a 16 entries table, as in the bootloader:
       ~6us per byte at 16MHz (a bit by bit update takes ~25us, longer than
       a data bit in mode 3, delaying the time stamp of the next edge) */
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
  #endif

  static inline void arm(uint16_t at) {
    OCR1A = at;
    TIFR1 = (1 << OCF1A);
    TIMSK1 |= (1 << OCIE1A);
  }

  static inline void close() {
    if((state >= DATA) && (state != IGNORE)) {
      filling = 0;
      end_us = PJON_MICROS();
    }
    state = IDLE;
    TIMSK1 &= ~(1 << OCIE1A);
    high_start = TCNT1;
  }

  /* Ignore the rest of a frame (not stored or not decoded) until the bus is
     quiet for 12 bits: inside a frame it never is (at most 8 bits and a
     padding bit), a transmitter waits for more before starting a frame. This
     avoids recognising data bits as a frame initializer. */
  static inline void ignore(uint16_t t) {
    close();
    state = IGNORE;
    arm(t + SWBB_T1_BIT * 12);
  }

  static inline void begin_byte() {
    state = DATA;
    pos = 0;
    bits = 0;
    arm(sync + (SWBB_T1_BIT * 19) / 2); // 9.5 bits
  }

  static inline void start_frame() {
    frames++;
    if(ready) { // Previous frame not consumed yet
      ignored++;
      ignore(sync);
      return;
    }
    wr = 0;
    filling = 1;
    ready = 1;
    acked = 0;
    #ifdef SWBB_INTERRUPT_ACK
      ack = 1;
      crc8 = 0;
      crc32 = 0xFFFFFFFF;
      length = PJON_PACKET_MAX_LENGTH;
    #endif
    // Each byte starts with its own synchronization pad
    state = PAD_LOW;
    arm(sync + SWBB_T1_BIT * 2);
  }

  // Sets the bits from slot a (included) to slot b (excluded)
  static inline void fill(uint8_t a, uint8_t b) {
    bits |= ((1 << b) - 1) & ~((1 << a) - 1);
  }

  static void acknowledge(); // defined after the class

  #ifdef SWBB_INTERRUPT_ACK
  static inline void check(uint16_t i, uint8_t b) {
    if(!ack) return;
    /* The CRC32 is updated before the header is known (byte 1): it covers
       all bytes but the last 4 */
    if(i < length - 4) {
      crc32 = crc32_update(crc32, b);
      if(i == length - 5) crc32 = ~crc32; // Transmitted MSB first
    }
    if(i < 3) crc8 = PJON_crc8::roll(b, crc8);
    if(i == 0) ack = (b == id);
    else if(i == 1) {
      header = b;
      ack = (b & PJON_ACK_REQ_BIT) && !(b & PJON_MODE_BIT) &&
        !(b & PJON_EXT_LEN_BIT);
    } else if(i == 2) {
      length = b;
      if(length < 5 || length > PJON_PACKET_MAX_LENGTH) ack = 0;
    } else if(i == 3) {
      ack = (b == crc8);
      crc8 = PJON_crc8::roll(b, crc8); // The CRC8 of the packet includes it
    }
    if(!ack || (i < 4)) return;
    if(header & PJON_CRC_BIT) {
      if(i >= length - 4) {
        if(b != (uint8_t)(crc32 >> 24)) ack = 0;
        crc32 <<= 8;
      }
    } else { // CRC8 of the whole packet (header CRC included)
      if(i < length - 1) crc8 = PJON_crc8::roll(b, crc8);
      else ack = (b == crc8);
    }
    if(ack && (i == length - 1)) acknowledge();
  }
  #endif

  // Stores the byte of slots 1-8, slot 0 (low synchronization bit) must be 0
  static inline bool finish_byte() {
    if((bits & 1) || (wr >= PJON_PACKET_MAX_LENGTH)) {
      ignore(TCNT1);
      return false;
    }
    uint8_t b = bits >> 1;
    uint16_t i = wr;
    buf[i] = b;
    wr = i + 1;
    #ifdef SWBB_INTERRUPT_ACK
      check(i, b);
      if(state == IDLE) return false; // Acknowledged, the frame is over
    #endif
    return true;
  }

  static inline void pad_end(uint16_t t) {
    uint16_t d = t - high_start;
    if((d >= SWBB_T1(SWBB_ACCEPTANCE)) && (d <= SWBB_T1(SWBB_WIRE_PAD_MAX))) {
      sync = t;
      begin_byte();
    } else close();
  }

  static inline void edge(uint16_t t, uint8_t l) {
    uint16_t d;
    switch(state) {
      case IDLE:
        if(l) high_start = t;
        else {
          d = t - high_start;
          if(
            (d >= SWBB_T1(SWBB_ACCEPTANCE)) &&
            (d <= SWBB_T1(SWBB_WIRE_PAD_MAX * SWBB_MAX_PREAMBLE))
          ) {
            pads = 1;
            sync = t;
            state = INIT;
          }
        }
        break;
      case INIT: // Frame initializer: 3 pads, each followed by 1 low bit
        if(l) {
          d = t - sync;
          if((d < SWBB_T1_BIT / 2) || (d > (SWBB_T1_BIT * 3) / 2))
            state = IDLE;
          high_start = t;
        } else {
          d = t - high_start;
          if(
            (d >= SWBB_T1(SWBB_ACCEPTANCE)) &&
            (d <= SWBB_T1(SWBB_WIRE_PAD_MAX))
          ) {
            sync = t;
            if(++pads == 3) start_frame();
          } else state = IDLE;
        }
        break;
      case DATA: {
        // Slots elapsed since the synchronization (rounded)
        d = t - sync;
        uint8_t p = pos;
        uint16_t limit = (uint16_t)p * SWBB_T1_BIT + SWBB_T1_BIT / 2;
        while((d >= limit) && (p < 10)) {
          p++;
          limit += SWBB_T1_BIT;
        }
        if(p == pos) { ignore(t); break; } // Two edges in the same bit
        if(!l) fill(pos, (p < 9) ? p : 9); // A high run ended
        if(p < 9) { pos = p; break; }
        pos = 9;
        if(!finish_byte()) break;
        if(l) { // Rising edge of the next padding bit
          high_start = t;
          state = PAD_HIGH;
          arm(t + SWBB_T1(SWBB_WIRE_PAD_MAX));
        } else { // Falling edge of a padding bit merged with 1 bits
          high_start = sync + SWBB_T1_BIT * 9;
          pad_end(t);
        }
        break;
      }
      case PAD_LOW:
        if(l) {
          high_start = t;
          state = PAD_HIGH;
          arm(t + SWBB_T1(SWBB_WIRE_PAD_MAX));
        }
        break;
      case PAD_HIGH:
        if(!l) pad_end(t);
        break;
      case IGNORE:
        arm(t + SWBB_T1_BIT * 12);
        break;
    }
  }

  // 9.5 bits after the synchronization: the last bits had no edge
  static inline void timeout() {
    switch(state) {
      case DATA:
        if(level) fill(pos, 9);
        if(!finish_byte()) return;
        if(level) { // Padding bit merged with the last 1 bits
          high_start = sync + SWBB_T1_BIT * 9;
          state = PAD_HIGH;
          arm(high_start + SWBB_T1(SWBB_WIRE_PAD_MAX));
        } else { // Wait for the padding bit for 1 more bit
          state = PAD_LOW;
          arm(sync + (SWBB_T1_BIT * 21) / 2);
        }
        break;
      default: // No padding bit or too long: the frame is over
        close();
    }
  }
}

class SoftwareBitBang : public SoftwareBitBangPolling {
  public:
    bool begin(uint8_t did = 0) {
      SoftwareBitBangPolling::begin(did);
      #ifdef SWBB_INTERRUPT_ACK
        swbb_irq::id = did;
      #endif
      TCCR1A = 0;
      TCCR1B = (1 << CS11); // F_CPU / 8
      TCCR1C = 0;
      #ifdef SWBB_IRQ_PIN_DEBUG // PD3 high while the interrupts run
        DDRD |= 8;
      #endif
      SWBB_EXT_INT_EICR =
        (SWBB_EXT_INT_EICR & ~SWBB_IRQ_SENSE_MASK) | SWBB_IRQ_SENSE;
      irq_on();
      return true;
    };


    /* Serve the bytes of the frame decoded by the interrupt: */

    uint16_t receive_frame(uint8_t *data, uint16_t max_length) {
      using namespace swbb_irq;
      if(max_length == PJON_PACKET_MAX_LENGTH) {
        if(_reading) { // The previous frame was consumed
          _reading = false;
          ready = 0;
        }
        if(!ready) return SWBB_FAIL;
        _reading = true;
        _consumed = false;
        _rd = 0;
      }
      /* Wait for the next byte for up to 2 bytes. The interrupt may store
         a byte (and end the frame, or send the acknowledgement for ~0.5ms)
         between the checks: availability is checked again before failing */
      uint32_t time = PJON_MICROS();
      while(_rd >= wr) {
        bool over = !filling || ((uint32_t)(PJON_MICROS() - time) >
          (uint32_t)(2 * (SWBB_WIRE_PAD_MAX + 9 * SWBB_TX_BIT)));
        if(over && (_rd >= wr)) return SWBB_FAIL;
      }
      *data = buf[_rd++];
      if(!filling && (_rd >= wr)) { // Frame consumed, free the buffer
        _reading = false;
        _end_us = end_us;
        _length = wr;
        _consumed = true;
        ready = 0;
      }
      return 1;
    };


    uint16_t receive_byte() {
      uint8_t b;
      if(receive_frame(&b, 0) == 1) return b;
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


    void send_response(uint8_t response) {
      if(swbb_irq::acked) { // Already sent by the interrupt
        swbb_irq::acked = 0;
        return;
      }
      /* The transmitter waits for the response for
         SWBB_RESPONSE_OFFSET * length + SWBB_LATENCY after the end of the
         frame: if that time is over do not transmit, it would disturb other
         exchanges */
      if(_consumed) { // The frame ended at _end_us
        _consumed = false;
        if(
          (uint32_t)(PJON_MICROS() - _end_us) >
          (uint32_t)(SWBB_RESPONSE_OFFSET * _length + SWBB_LATENCY)
        ) return;
      }
      irq_off();
      SoftwareBitBangPolling::send_response(response);
      irq_on();
    };


    static void irq_off() {
      EIMSK &= ~SWBB_IRQ_ENABLE;
      TIMSK1 &= ~(1 << OCIE1A);
      swbb_irq::state = swbb_irq::IDLE;
      if(swbb_irq::filling) swbb_irq::filling = 0;
    };


    static void irq_on() {
      uint8_t sreg = SREG;
      cli();
      swbb_irq::state = swbb_irq::IDLE;
      swbb_irq::level = (SWBB_IN >> SWBB_BIT) & 1;
      swbb_irq::high_start = TCNT1;
      EIFR = SWBB_IRQ_FLAG;
      EIMSK |= SWBB_IRQ_ENABLE;
      SREG = sreg;
    };

  private:
    uint16_t _rd = 0;
    bool _reading = false;
    bool _consumed = false;
    uint16_t _length = 0;
    uint32_t _end_us = 0;
};

namespace swbb_irq {
  /* Synchronous acknowledgement sent from the interrupt (the transmitter is
     sending its keep busy bits): the interrupt keeps running for the
     duration of the response (~0.5ms) */
  static void acknowledge() {
    EIMSK &= ~SWBB_IRQ_ENABLE;
    SoftwareBitBangPolling::bus_release();
    // Same as SoftwareBitBangPolling::send_response, interrupts are disabled
    SoftwareBitBangPolling::wait_while_low(
      SWBB_LOOPS(SWBB_RESPONSE_PULSE_WAIT)
    );
    SoftwareBitBangPolling::wait_while_high(SWBB_LOOPS(SWBB_WIRE_BIT / 2));
    __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_RESPONSE_DELAY));
    SoftwareBitBangPolling::bus_drive_high();
    SoftwareBitBangPolling::send_pad(0);
    SoftwareBitBangPolling::send_byte(PJON_ACK, 0);
    SoftwareBitBangPolling::bus_release();
    acked = 1;
    close();
    filling = 0;
    level = 0;
    EIFR = SWBB_IRQ_FLAG;
    EIMSK |= SWBB_IRQ_ENABLE;
  }
}

#ifndef SWBB_STATE_EXTERN
ISR(SWBB_IRQ_VECT) {
  uint16_t t = TCNT1;
  uint8_t l = (SWBB_IN >> SWBB_BIT) & 1;
  if(l == swbb_irq::level) { // An edge was missed
    swbb_irq::close();
    return;
  }
  swbb_irq::level = l;
  #ifdef SWBB_IRQ_PIN_DEBUG
    PORTD |= 8;
  #endif
  swbb_irq::edge(t, l);
  #ifdef SWBB_IRQ_PIN_DEBUG
    PORTD &= ~8;
  #endif
}

ISR(TIMER1_COMPA_vect) {
  #ifdef SWBB_IRQ_PIN_DEBUG
    PORTD |= 8;
  #endif
  swbb_irq::timeout();
  #ifdef SWBB_IRQ_PIN_DEBUG
    PORTD &= ~8;
  #endif
}
#endif
