
/* SoftwareBitBang
   1 or 2 wires software-defined asynchronous serial data link layer
   used as a Strategy by PJON (included in version v3.0)
   Compliant with PJDL (Padded Jittering Data Link) specification v5.0
   ___________________________________________________________________________

    Copyright 2010-2025 Giovanni Blu Mitolo gioscarab@gmail.com

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

/* Set here the selected transmission mode - default STANDARD */
#ifndef SWBB_MODE
  #define SWBB_MODE         1
#endif

// Used to signal communication failure
#define SWBB_FAIL       65535
// Used for pin handling
#define SWBB_NOT_ASSIGNED 255

/* Transmission speed modes (see Timing.h)
  MODE   1: 1.97kB/s - 15808Bd
  MODE   2: 2.21kB/s - 17696Bd
  MODE   3: 3.10kB/s - 24844Bd
  MODE   4: 3.34kB/s - 26755Bd
  MODE   5: 6.3kB/s, MODE 6: 8.2kB/s (not PJDL, SWBB_PIN only, see Timing.h) */
#include "Timing.h"

// Recommended receive time for this strategy, in microseconds
#ifndef SWBB_RECEIVE_TIME
  #define SWBB_RECEIVE_TIME 1000
#endif

/* Direct port access (optional, ATmega88/168/328/328P/328PB only)
   Define SWBB_PIN before including PJON to select the pin at compile time:
     #define SWBB_PIN 2
     #include <PJONSoftwareBitBang.h>
   The pin is then read and written with single instructions and the timing
   is cycle exact (as in the bootloader) instead of using digitalRead,
   digitalWrite, pinMode and the per-MCU adjustments of Timing.h.
   set_pin and set_pins are accepted for compatibility but ignored.
   It is compatible with the portable implementation and the bootloader.

   Interrupts are disabled while each byte is transmitted or received
   (about 0.3ms in mode 3, 0.5ms in mode 1) and served during the padding
   bits, so they can not distort the data bits. Timer0 (running with the
   Arduino core's 64 prescaler, used for millis) is read to compensate the
   time spent serving interrupts. Define SWBB_ATOMIC 0 to keep interrupts
   enabled (not recommended: an interrupt during a byte corrupts it). */

#ifdef SWBB_PIN
  #if !defined(__AVR_ATmega88__)  && !defined(__AVR_ATmega88P__)  && \
      !defined(__AVR_ATmega168__) && !defined(__AVR_ATmega168P__) && \
      !defined(__AVR_ATmega328__) && !defined(__AVR_ATmega328P__) && \
      !defined(__AVR_ATmega328PB__)
    #error "SWBB_PIN (direct port access) supports only ATmega88/168/328"
  #endif
  #if SWBB_PIN >= 0 && SWBB_PIN <= 7
    #define SWBB_PORT PORTD
    #define SWBB_DDR  DDRD
    #define SWBB_IN   PIND
    #define SWBB_BIT  (SWBB_PIN)
  #elif SWBB_PIN >= 8 && SWBB_PIN <= 13
    #define SWBB_PORT PORTB
    #define SWBB_DDR  DDRB
    #define SWBB_IN   PINB
    #define SWBB_BIT  (SWBB_PIN - 8)
  #elif SWBB_PIN >= 14 && SWBB_PIN <= 19
    #define SWBB_PORT PORTC
    #define SWBB_DDR  DDRC
    #define SWBB_IN   PINC
    #define SWBB_BIT  (SWBB_PIN - 14)
  #else
    #error "SWBB_PIN must be an Arduino pin number between 0 and 19"
  #endif
  #ifndef SWBB_ATOMIC
    #define SWBB_ATOMIC 1
  #endif
  // Microseconds to cycles
  #define SWBB_CYCLES(US) ((uint32_t)((US) * (F_CPU / 1000000.0)))
  // Microseconds to iterations of the 6 cycles edge waiting loop
  #define SWBB_LOOPS(US) ((uint16_t)(SWBB_CYCLES(US) / 6))
  #define SWBB_INLINE inline __attribute__((always_inline))
  /* Data bit loops: OVERHEAD cycles + 4 cycles per delay iteration + nops */
  #define SWBB_BIT_LOOPS(BIT, OVERHEAD) ((SWBB_CYCLES(BIT) - (OVERHEAD)) / 4)
  #define SWBB_BIT_NOPS(BIT, OVERHEAD) ((SWBB_CYCLES(BIT) - (OVERHEAD)) % 4)
#endif

/* Mode 7 (not PJDL, see SoftwareBitBangM7.h): the PJDL class below is
   compiled as SoftwareBitBangPolling for its direct port access helpers */
#if SWBB_MODE == 7
  #ifdef SWBB_INTERRUPT
    #error "SWBB_MODE 7 supports SWBB_INTERRUPT_WAKE, not SWBB_INTERRUPT"
  #endif
  #define SoftwareBitBang SoftwareBitBangPolling
#endif

/* Interrupt driven reception (prototype, see SoftwareBitBangIRQ.h) */
#ifdef SWBB_INTERRUPT
  #ifndef SWBB_PIN
    #error "SWBB_INTERRUPT requires SWBB_PIN"
  #endif
  #if SWBB_MODE > 4
    /* Bits of 8-12us are shorter than the jitter of the edge timestamps
       (the interrupt waits for any other one being served, e.g. Timer0) */
    #error "SWBB_INTERRUPT supports modes 1-4 only"
  #endif
  #define SoftwareBitBang SoftwareBitBangPolling
#endif

/* Interrupt triggered reception (prototype, see SoftwareBitBangWake.h) */
#ifdef SWBB_INTERRUPT_WAKE
  #ifndef SWBB_PIN
    #error "SWBB_INTERRUPT_WAKE requires SWBB_PIN"
  #endif
  #ifdef SWBB_INTERRUPT
    #error "Define either SWBB_INTERRUPT or SWBB_INTERRUPT_WAKE"
  #endif
  #define SoftwareBitBang SoftwareBitBangPolling
#endif

class SoftwareBitBang {
  public:
    /* Returns the delay related to the attempts passed as parameter: */

    uint32_t back_off(uint8_t attempts) {
      uint32_t result = attempts;
      for(uint8_t d = 0; d < SWBB_BACK_OFF_DEGREE; d++)
        result *= (uint32_t)(attempts);
      return result;
    };


    /* Begin method, to be called on initialization:
       (returns always true) */

    bool begin(uint8_t did = 0) {
      PJON_DELAY(PJON_RANDOM(SWBB_INITIAL_DELAY) + did);
      return true;
    };


    /* Returns the maximum number of attempts for each transmission: */

    static uint8_t get_max_attempts() {
      return SWBB_MAX_ATTEMPTS;
    };


    /* Returns the recommended receive time for this strategy: */

    static uint16_t get_receive_time() {
      return SWBB_RECEIVE_TIME;
    };


    /* Handle a collision: */

    void handle_collision() {
      PJON_DELAY_MICROSECONDS(PJON_RANDOM(SWBB_COLLISION_DELAY));
    };

#ifdef SWBB_PIN
/* Direct port access implementation ----------------------------------------

   The bus is driven high only after the output register is set high and it
   is released (input, no pull-up) without driving it low, so a device never
   pulls the bus against another one transmitting.

   Edges are detected by loops of exactly 6 cycles (0.375us at 16MHz). */

    /* Waits while the bus is high, for up to loops iterations of 6 cycles.
       Returns the iterations left (0 if the bus is still high). */

    SWBB_INLINE static uint16_t wait_while_high(uint16_t loops) {
      __asm__ __volatile__ (
        "1: sbis %[in], %[bit]  \n\t"
        "   rjmp 2f             \n\t"
        "   sbiw %[n], 1        \n\t"
        "   brne 1b             \n\t"
        "2:                     \n\t"
        : [n] "+w" (loops)
        : [in] "I" (_SFR_IO_ADDR(SWBB_IN)), [bit] "I" (SWBB_BIT)
      );
      return loops;
    };


    /* Waits while the bus is low, for up to loops iterations of 6 cycles.
       Returns the iterations left (0 if the bus is still low). */

    SWBB_INLINE static uint16_t wait_while_low(uint16_t loops) {
      __asm__ __volatile__ (
        "1: sbic %[in], %[bit]  \n\t"
        "   rjmp 2f             \n\t"
        "   sbiw %[n], 1        \n\t"
        "   brne 1b             \n\t"
        "2:                     \n\t"
        : [n] "+w" (loops)
        : [in] "I" (_SFR_IO_ADDR(SWBB_IN)), [bit] "I" (SWBB_BIT)
      );
      return loops;
    };


    SWBB_INLINE static bool bus_high() {
      return SWBB_IN & (1 << SWBB_BIT);
    };


    SWBB_INLINE static void bus_set() {
      SWBB_PORT |= (1 << SWBB_BIT);
    };


    SWBB_INLINE static void bus_clear() {
      SWBB_PORT &= ~(1 << SWBB_BIT);
    };


    SWBB_INLINE static void bus_drive_high() {
      SWBB_PORT |= (1 << SWBB_BIT);
      SWBB_DDR |= (1 << SWBB_BIT);
    };


    /* Input without pull-up, the pull-down resistor keeps the bus low.
       If this device is driving the bus it drives it low for
       SWBB_DISCHARGE_US first: the pull-down alone (often 100k) would take
       ~100us to discharge the cable capacitance (~1-2nF for 20m), and the
       slow falling edge would look like a padding bit. Devices start
       transmitting at least SWBB_RESPONSE_DELAY after a falling edge, so
       this short low level can not collide with them. */

    SWBB_INLINE static void bus_release() {
      if(SWBB_DDR & (1 << SWBB_BIT)) {
        SWBB_PORT &= ~(1 << SWBB_BIT);
        __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_DISCHARGE_US));
        SWBB_DDR &= ~(1 << SWBB_BIT);
      } else SWBB_PORT &= ~(1 << SWBB_BIT);
    };


    /* Disables interrupts if SWBB_ATOMIC, returns the previous state */

    SWBB_INLINE static uint8_t atomic_begin() {
      uint8_t sreg = SREG;
      #if SWBB_ATOMIC
        cli();
      #endif
      return sreg;
    };


    SWBB_INLINE static void atomic_end(uint8_t sreg) {
      SREG = sreg;
    };


    /* Busy waits 4 * loops cycles (loops > 0) */

    SWBB_INLINE static void delay_loops(uint16_t loops) {
      __asm__ __volatile__ (
        "1: sbiw %[n], 1  \n\t"
        "   brne 1b       \n\t"
        : [n] "+w" (loops)
      );
    };


    /* Called when the rising edge of a padding bit is transmitted, returns
       at its falling edge (the caller clears the pin). If SWBB_ATOMIC,
       pending interrupts are served here and the time they take, measured
       with Timer0 (64 cycles per tick as configured by the Arduino core),
       is subtracted from the padding bit so its duration stays within
       -4us/+4us of the expected one. */

    SWBB_INLINE static void pad_wait(uint8_t sreg) {
      #if SWBB_ATOMIC
        uint8_t start = TCNT0;
        SREG = sreg;
        __asm__ __volatile__ ("nop\n\tnop\n\tnop\n\tnop" ::: "memory");
        cli();
        uint16_t used = (uint8_t)(TCNT0 - start) * (64 / 4);
        uint16_t loops = (SWBB_CYCLES(SWBB_WIRE_PAD) - 12) / 4;
        delay_loops((used < loops) ? (loops - used) : 1);
      #else
        (void)sreg;
        __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_WIRE_PAD));
      #endif
    };


    /* Check if the channel is free for transmission: no high level for the
       duration of a padding bit, 9.5 data bits and the latency, then for a
       small random time. The bus is monitored continuously, so also short
       keep busy bits of an ongoing exchange are detected. */

    bool can_start() {
      bus_release();
      if(wait_while_low(SWBB_LOOPS(
        SWBB_WIRE_PAD + (SWBB_WIRE_BIT * 9.5) + SWBB_LATENCY
      ))) return false;
      uint8_t random_delay = PJON_RANDOM(SWBB_COLLISION_DELAY);
      if(wait_while_low(
        ((uint16_t)random_delay * (F_CPU / 1000000UL)) / 6 + 1
      )) return false;
      return true;
    };


    /* Read a byte from the pin (call it at the beginning of the first bit),
       samples in the middle of each bit, returns at the end of the last.
       The bit loop is written in assembly so its duration is exact:
       8 cycles + 4 * SWBB_BIT_LOOPS + SWBB_BIT_NOPS = 1 data bit. */

    static uint8_t read_byte() {
      uint8_t value = 0, i = 8;
      uint16_t n;
      __builtin_avr_delay_cycles(
        (SWBB_CYCLES(SWBB_RX_BIT) / 2) - (SWBB_SAMPLE_ADVANCE / 2)
      );
      __asm__ __volatile__ (
        "1: lsr  %[v]               \n\t"
        "   sbic %[in], %[bit]      \n\t"
        "   ori  %[v], 0x80         \n\t"
        "   dec  %[i]               \n\t"
        "   breq 3f                 \n\t"
        "   ldi  %A[n], lo8(%[loops]) \n\t"
        "   ldi  %B[n], hi8(%[loops]) \n\t"
        "2: sbiw %[n], 1            \n\t"
        "   brne 2b                 \n\t"
        "   .rept %[nops]           \n\t"
        "   nop                     \n\t"
        "   .endr                   \n\t"
        "   rjmp 1b                 \n\t"
        "3:                         \n\t"
        : [v] "+d" (value), [i] "+r" (i), [n] "=&w" (n)
        : [in] "I" (_SFR_IO_ADDR(SWBB_IN)), [bit] "I" (SWBB_BIT),
          [loops] "i" (SWBB_BIT_LOOPS(SWBB_RX_BIT, 8)),
          [nops] "i" (SWBB_BIT_NOPS(SWBB_RX_BIT, 8))
      );
      __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_RX_BIT) / 2);
      return value;
    };


    /* Synchronization pad: a padding bit (high) followed by a low data bit.
       Returns true at the end of the low bit if the pattern is recognised.
       If the bus is low when called (the previous bit was a 0 slightly
       longer than expected) its rising edge is awaited for up to one bit.
       max_high is the maximum padding bit duration in loop iterations.
       If continued is true the padding bit started when the previous byte
       (or synchronization pad) ended, at _end: the time elapsed since then,
       measured with Timer0, is added to the measured duration. This way a
       padding bit is recognised even if the receiver was late (interrupts
       served or the received data processed between bytes). */

    bool sync(uint16_t max_high, bool continued) {
      uint16_t late = 0;
      if(bus_high()) {
        if(continued) late = (uint8_t)(TCNT0 - _end) * (64 / 6);
      } else if(!wait_while_low(SWBB_LOOPS(SWBB_RX_BIT))) return false;
      uint16_t left = wait_while_high(max_high);
      if(!left) return false;
      if((uint16_t)(max_high - left + late) < SWBB_LOOPS(SWBB_ACCEPTANCE))
        return false;
      __builtin_avr_delay_cycles(
        SWBB_CYCLES(SWBB_RX_BIT / 2) - (SWBB_SAMPLE_ADVANCE / 2)
      );
      if(bus_high()) return false;
      __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_RX_BIT / 2));
      _end = TCNT0;
      return true;
    };


    bool sync() {
      return sync(SWBB_LOOPS(SWBB_WIRE_PAD_MAX), true);
    };


    /* Receive byte if in sync: */

    uint16_t receive_byte() {
      uint8_t sreg = atomic_begin();
      uint16_t result = SWBB_FAIL;
      if(sync()) {
        result = read_byte();
        _end = TCNT0;
      }
      atomic_end(sreg);
      return result;
    };


    /* Receive a frame: */

    uint16_t receive_frame(uint8_t *data, uint16_t max_length) {
      if(max_length == PJON_PACKET_MAX_LENGTH) {
        // Look for a frame initializer (returns at once if the bus is low)
        if(!bus_high()) return SWBB_FAIL;
        bus_release();
        #if SWBB_MAX_PREAMBLE > 1
          // A long preamble is awaited with interrupts enabled
          uint8_t sreg = SREG;
        #else
          uint8_t sreg = atomic_begin();
        #endif
        bool ok = sync(SWBB_LOOPS(SWBB_WIRE_PAD_MAX * SWBB_MAX_PREAMBLE), false);
        atomic_end(sreg); // Interrupts are served at the next padding bit
        if(!ok) return SWBB_FAIL;
        sreg = atomic_begin();
        ok = sync();
        atomic_end(sreg);
        if(!ok) return SWBB_FAIL;
        sreg = atomic_begin();
        ok = sync();
        atomic_end(sreg);
        if(!ok) return SWBB_FAIL;
      } // Receive one byte
      uint16_t result = receive_byte();
      if(result == SWBB_FAIL) return SWBB_FAIL;
      *data = result;
      return 1;
    };


    /* Receive byte response:
       Transmitter emits a keep busy bit and waits SWBB_RESPONSE_LISTEN
       microseconds for the rising edge of the response, cyclically for up
       to the response timeout. The response is prepended by 2 sync pads. */

    uint16_t receive_response() {
      uint32_t time = PJON_MICROS();
      while((uint32_t)(PJON_MICROS() - time) < _timeout) {
        uint8_t sreg = atomic_begin();
        if(!bus_high()) { // Do not drive the bus if the response started
          bus_drive_high();
          __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_WIRE_KEEP_BUSY));
          bus_release();
          /* bus_release drives the bus low before releasing it, so it is
             already low here. Safety margin: wait up to SWBB_RELEASE_FALL
             for it to read low (without the discharge it took ~6us on a
             bench, ~120us expected with 20m of cable and a 100k
             pull-down), otherwise the keep busy bit is read as the start
             of the response and the sync fails */
          wait_while_high(SWBB_LOOPS(SWBB_RELEASE_FALL));
          wait_while_low(SWBB_LOOPS(SWBB_RESPONSE_LISTEN));
        }
        if(bus_high()) {
          uint16_t response = SWBB_FAIL;
          if(sync(SWBB_LOOPS(SWBB_WIRE_PAD_MAX), false))
            response = receive_byte();
          atomic_end(sreg);
          // A response was detected, do not keep the bus busy any longer
          return response;
        }
        atomic_end(sreg);
      }
      return SWBB_FAIL;
    };


    /* Transmit a byte prepended by a synchronization pad (the bus must be
       driven by this device). Interrupts are served during the padding bit
       (see pad_wait). The bit loop is written in assembly so its duration
       is exact:
       11 cycles + 4 * SWBB_BIT_LOOPS + SWBB_BIT_NOPS = 1 data bit. */

    static void send_byte(uint8_t b, uint8_t sreg) {
      bus_set();
      pad_wait(sreg);
      bus_clear();
      __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_TX_BIT) - 2);
      uint8_t i = 8;
      uint16_t n;
      __asm__ __volatile__ (
        "1: sbrc %[b], 0            \n\t"
        "   sbi  %[port], %[bit]    \n\t"
        "   sbrs %[b], 0            \n\t"
        "   cbi  %[port], %[bit]    \n\t"
        "   lsr  %[b]               \n\t"
        "   ldi  %A[n], lo8(%[loops]) \n\t"
        "   ldi  %B[n], hi8(%[loops]) \n\t"
        "2: sbiw %[n], 1            \n\t"
        "   brne 2b                 \n\t"
        "   .rept %[nops]           \n\t"
        "   nop                     \n\t"
        "   .endr                   \n\t"
        "   dec  %[i]               \n\t"
        "   brne 1b                 \n\t"
        : [b] "+r" (b), [i] "+r" (i), [n] "=&w" (n)
        : [port] "I" (_SFR_IO_ADDR(SWBB_PORT)), [bit] "I" (SWBB_BIT),
          [loops] "i" (SWBB_BIT_LOOPS(SWBB_TX_BIT, 11)),
          [nops] "i" (SWBB_BIT_NOPS(SWBB_TX_BIT, 11))
      );
    };


    void send_byte(uint8_t b) {
      uint8_t sreg = atomic_begin();
      send_byte(b, sreg);
      atomic_end(sreg);
    };


    /* Send byte response:
       Synchronize to the falling edge of a keep busy bit, wait
       SWBB_RESPONSE_DELAY and transmit the response prepended by an
       additional synchronization pad. */

    void send_response(uint8_t response) {
      bus_release();
      uint8_t sreg = atomic_begin();
      wait_while_low(SWBB_LOOPS(SWBB_RESPONSE_PULSE_WAIT));
      wait_while_high(SWBB_LOOPS(SWBB_WIRE_BIT / 2));
      __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_RESPONSE_DELAY));
      bus_drive_high();
      send_pad(sreg);
      send_byte(response, sreg);
      bus_release();
      atomic_end(sreg);
    };


    /* Transmit a synchronization pad (the bus must be driven) */

    static void send_pad(uint8_t sreg) {
      bus_set();
      pad_wait(sreg);
      bus_clear();
      __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_TX_BIT));
    };


    /* Send a frame: preamble, frame initializer and data */

    void send_frame(uint8_t *data, uint16_t length) {
      _timeout = (length * SWBB_RESPONSE_OFFSET) + SWBB_LATENCY;
      #if SWBB_PREAMBLE != 1
        /* Long preamble, transmitted with interrupts enabled. Its duration
           is SWBB_BIT_SPACER * SWBB_PREAMBLE, as transmitted by the
           portable implementation: receivers using it do not accept a
           longer one. */
        bus_drive_high();
        uint32_t time = PJON_MICROS();
        while(
          (uint32_t)(PJON_MICROS() - time) <
          ((uint32_t)SWBB_BIT_SPACER * SWBB_PREAMBLE)
        );
        uint8_t sreg = atomic_begin();
        bus_clear();
        __builtin_avr_delay_cycles(SWBB_CYCLES(SWBB_TX_BIT));
      #else
        uint8_t sreg = atomic_begin();
        bus_drive_high();
        send_pad(sreg);
      #endif
      send_pad(sreg);
      send_pad(sreg);
      for(uint16_t b = 0; b < length; b++)
        send_byte(data[b], sreg);
      bus_release();
      atomic_end(sreg);
    };


    /* The pin is selected at compile time with SWBB_PIN, these methods are
       kept for compatibility: */

    void set_pin(uint8_t pin) {
      (void)pin;
      bus_release();
    };


    void set_pins(
      uint8_t input_pin = SWBB_NOT_ASSIGNED,
      uint8_t output_pin = SWBB_NOT_ASSIGNED
    ) {
      (void)input_pin;
      (void)output_pin;
      bus_release();
    };

  private:
    uint16_t _timeout;
    uint8_t  _end; // Timer0 count at the end of the last byte received
};

#else
/* Portable implementation (digitalRead, digitalWrite, pinMode) ---------- */

    /* Check if the channel is free for transmission: the bus is polled
       continuously for 1 padding bit, 9.5 data bits and the latency, if no
       1 is detected there is no active transmission. Polling continuously
       (rather than sampling once per bit) also detects the short keep busy
       bits transmitted while a device waits for a response. */

    bool can_start() {
      PJON_IO_MODE(_input_pin, INPUT);
      uint32_t time = PJON_MICROS();
      while(
        (uint32_t)(PJON_MICROS() - time) <
        (uint32_t)(SWBB_BIT_SPACER + (SWBB_BIT_WIDTH * 9.5) + SWBB_LATENCY)
      ) if(PJON_IO_READ(_input_pin)) return false;
      // Delay for a small random time and then check again
      PJON_DELAY_MICROSECONDS(PJON_RANDOM(SWBB_COLLISION_DELAY));
      if(PJON_IO_READ(_input_pin)) return false;
      return true;
    };


    /* Read a byte from the pin */

    uint8_t read_byte() {
      uint8_t byte_value = 0B00000000;
      // Delay until the center of the first bit
      PJON_DELAY_MICROSECONDS(SWBB_BIT_WIDTH / 2);
      for(uint8_t i = 0; i < 7; i++) {
        // Read in the center of the bit
        byte_value += PJON_IO_READ(_input_pin) << i;
        // Delay until the center of the next one
        PJON_DELAY_MICROSECONDS(SWBB_BIT_WIDTH);
      }
      // Read in the center of the last one
      byte_value += PJON_IO_READ(_input_pin) << 7;
      // Delay until the end of the last bit
      PJON_DELAY_MICROSECONDS(SWBB_BIT_WIDTH / 2);
      return byte_value;
    };


    /* Receive byte if in sync: */

    uint16_t receive_byte() {
      if(sync()) return read_byte();
      return SWBB_FAIL;
    };


    /* Receive byte response:
       Transmitter emits a SWBB_BIT_WIDTH / 4 long bit and tries
       to get a response cyclically for SWBB_RESPONSE_TIMEOUT microseconds.
       Receiver synchronizes to the falling edge of the last incoming
       bit and transmits PJON_ACK */

    uint16_t receive_response() {
      if(_output_pin != _input_pin && _output_pin != SWBB_NOT_ASSIGNED)
        PJON_IO_WRITE(_output_pin, LOW);
      uint16_t response = SWBB_FAIL;
      uint32_t time = PJON_MICROS();
      while((uint32_t)(PJON_MICROS() - time) < _timeout) {
        PJON_IO_WRITE(_input_pin, LOW);
        if(sync()) response = receive_byte();
        if(response == SWBB_FAIL) {
          drive_high(_output_pin);
          PJON_DELAY_MICROSECONDS(SWBB_BIT_WIDTH / 4);
          release(_output_pin);
        } else return response;
      }
      return response;
    };


    /* Receive a frame: */

    uint16_t receive_frame(uint8_t *data, uint16_t max_length) {
      uint16_t result;
      if(max_length == PJON_PACKET_MAX_LENGTH) {
        uint32_t time = PJON_MICROS();
        // Look for a frame initializer
        if(!sync_preamble() || !sync() || !sync()) return SWBB_FAIL;
        // Check its timing consistency
        if(
          (uint32_t)(PJON_MICROS() - time) <
          (((SWBB_BIT_WIDTH * 3) + (SWBB_BIT_SPACER * 3)) - SWBB_ACCEPTANCE)
        ) return SWBB_FAIL;
      } // Receive one byte
      result = receive_byte();
      if(result == SWBB_FAIL) return SWBB_FAIL;
      *data = result;
      return 1;
    };


    /* Every byte is prepended with a synchronization pad made by 2
       padding bits. The first is a longer than standard logic 1 followed
       by a standard logic 0.
       __________ ___________________________
      | SyncPad  | Byte                      |
      |______    |___       ___     _____    |
      |  |   |   |   |     |   |   |     |   |
      |  | 1 | 0 | 1 | 0 0 | 1 | 0 | 1 1 | 0 |
      |__|___|___|___|_____|___|___|_____|___|
         |
      Minimum acceptable HIGH padding bit duration

    The reception tecnique is based on finding a logic 1 as long as the
    first padding bit within a certain threshold, synchronizing to its
    falling edge and checking if it is followed by a logic 0. If this
    pattern is recognised, reception starts, if not, interference,
    synchronization loss or simply absence of communication is
    detected at byte level. */

    void send_byte(uint8_t b) {
      pulse(1);
      for(uint8_t mask = 0x01; mask; mask <<= 1) {
        PJON_IO_WRITE(_output_pin, b & mask);
        PJON_DELAY_MICROSECONDS(SWBB_BIT_WIDTH);
      }
    };


    /* Send byte response:
       Transmitter sends a SWBB_BIT_WIDTH / 4 microseconds long HIGH bit and
       tries to receive a response cyclically for SWBB_RESPONSE_TIMEOUT
       microseconds. Receiver synchronizes to the falling edge of the last
       incoming bit and transmits its response.
       The keep busy bit is transmitted every ~50us, wait for it for up to
       SWBB_RESPONSE_PULSE_WAIT. After its falling edge wait before
       transmitting: the transmitter may still be driving its pin low. */

    void send_response(uint8_t response) {
      PJON_IO_PULL_DOWN(_input_pin);
      uint32_t time = PJON_MICROS();
      while( // If initially low Wait for the next high
        ((uint32_t)(PJON_MICROS() - time) < SWBB_RESPONSE_PULSE_WAIT) &&
        !PJON_IO_READ(_input_pin)
      );
      time = PJON_MICROS();
      while( // If high Wait for low
        ((uint32_t)(PJON_MICROS() - time) < (SWBB_BIT_WIDTH / 2)) &&
        PJON_IO_READ(_input_pin)
      );
      #if SWBB_RESPONSE_DELAY_PORTABLE > 0
        PJON_DELAY_MICROSECONDS(SWBB_RESPONSE_DELAY_PORTABLE);
      #endif
      // Transmit response prepended with a synchronization pad
      drive_high(_output_pin);
      pulse(1);
      send_byte(response);
      release(_output_pin);
    };


    /* The data is prepended with a frame initializer composed by 3
       synchronization pads to signal the start of a frame.
     _________________ __________________________________
    |   FRAME INIT    | DATA 1-65535 bytes               |
    |_____ _____ _____|________________ _________________|
    |Sync |Sync |Sync |Sync | Byte     |Sync | Byte      |
    |___  |___  |___  |___  |     __   |___  |      _   _|
    |   | |   | |   | |   | |    |  |  |   | |     | | | |
    | 1 |0| 1 |0| 1 |0| 1 |0|0000|11|00| 1 |0|00000|1|0|1|
    |___|_|___|_|___|_|___|_|____|__|__|___|_|_____|_|_|_|

    Send a frame: */

    void send_frame(uint8_t *data, uint16_t length) {
      _timeout = (length * SWBB_RESPONSE_OFFSET) + SWBB_LATENCY;
      PJON_IO_MODE(_output_pin, OUTPUT);
      pulse(3); // Send frame initializer
      for(uint16_t b = 0; b < length; b++)
        send_byte(data[b]); // Send each byte
      release(_output_pin);
    };


    /* Check if a synchronization pad is incoming:
     __________
    | SyncPad  |
    |______    |
    |  |   |   |
    |  | 1 | 0 |
    |__|___|___|
       |
    Minimum acceptable HIGH padding bit duration

    The reception tecnique is based on finding a logic 1 as long as the
    first padding bit within a certain threshold, synchronizing to its
    falling edge and checking if it is followed by a logic 0. If this
    pattern is recognised, synchronization may have been obtained, if
    not, interference, synchronization loss or simply absence of
    communication is detected at byte level: */

    bool sync(uint32_t spacer) {
      PJON_IO_PULL_DOWN(_input_pin);
      if((_output_pin != _input_pin) && (_output_pin != SWBB_NOT_ASSIGNED))
        PJON_IO_PULL_DOWN(_output_pin);
      uint32_t time = PJON_MICROS();
      while(
        PJON_IO_READ(_input_pin) &&
        ((uint32_t)(PJON_MICROS() - time) <= spacer)
      );
      time = PJON_MICROS() - time;
      if(time < SWBB_ACCEPTANCE)
        return false;
      else {
        PJON_DELAY_MICROSECONDS((SWBB_BIT_WIDTH / 2) - SWBB_READ_DELAY);
        if(!PJON_IO_READ(_input_pin)) {
          PJON_DELAY_MICROSECONDS(SWBB_BIT_WIDTH / 2);
          return true;
        }
      }
      return false;
    };

    bool sync() {
      return sync(SWBB_BIT_SPACER);
    }

    bool sync_preamble() {
      return sync(SWBB_BIT_SPACER * SWBB_MAX_PREAMBLE);
    };

    /* Emit synchronization pulse: */

    void pulse(uint8_t n) {
      #if SWBB_PREAMBLE != 1
      if (n == 3) {
        // Transmit preamble
        PJON_IO_WRITE(_output_pin, HIGH);
        PJON_DELAY_MICROSECONDS(SWBB_BIT_SPACER * SWBB_PREAMBLE);
        PJON_IO_WRITE(_output_pin, LOW);
        PJON_DELAY_MICROSECONDS(SWBB_BIT_WIDTH);
        n--;
      }
      #endif
      while(n--) {
        PJON_IO_WRITE(_output_pin, HIGH);
        PJON_DELAY_MICROSECONDS(SWBB_BIT_SPACER);
        PJON_IO_WRITE(_output_pin, LOW);
        PJON_DELAY_MICROSECONDS(SWBB_BIT_WIDTH);
      }
    };

    /* Set the communicaton pin: */

    void set_pin(uint8_t pin) {
      PJON_IO_PULL_DOWN(pin);
      _input_pin = pin;
      _output_pin = pin;
    };


    /* Set a pair of communication pins: */

    void set_pins(
      uint8_t input_pin = SWBB_NOT_ASSIGNED,
      uint8_t output_pin = SWBB_NOT_ASSIGNED
    ) {
      if(input_pin != SWBB_NOT_ASSIGNED) PJON_IO_PULL_DOWN(input_pin);
      if(output_pin != SWBB_NOT_ASSIGNED) PJON_IO_PULL_DOWN(output_pin);
      _input_pin = input_pin;
      _output_pin = output_pin;
    };

  private:
    /* Drive the pin high. On AVR the output register is set before the pin
       is switched to output, so it is never driven low against a device
       already transmitting (while switching it is pulled up instead). */

    static void drive_high(uint8_t pin) {
      #ifdef __AVR__
        PJON_IO_WRITE(pin, HIGH);
        PJON_IO_MODE(pin, OUTPUT);
      #else
        PJON_IO_MODE(pin, OUTPUT);
        PJON_IO_WRITE(pin, HIGH);
      #endif
    };


    /* Release the pin: driven low first (digitalWrite then pinMode keep
       it low for a few microseconds), so the cable capacitance is
       discharged quickly instead of through the pull-down resistor. */

    static void release(uint8_t pin) {
      PJON_IO_PULL_DOWN(pin);
    };

    uint16_t _timeout;
    uint8_t  _input_pin;
    uint8_t  _output_pin;
};
#endif

#ifdef SWBB_INTERRUPT
  #undef SoftwareBitBang
  #include "SoftwareBitBangIRQ.h"
#endif

#if SWBB_MODE == 7
  #undef SoftwareBitBang
  #include "SoftwareBitBangM7.h"
#elif defined(SWBB_INTERRUPT_WAKE)
  #undef SoftwareBitBang
  #include "SoftwareBitBangWake.h"
#endif
