
/* PJON SoftwareBitBang strategy Transmission Timing table
   Copyright 2010-2025, Giovanni Blu Mitolo All rights reserved.

   Often timing in two different machines do not match, code execution
   time can variate and time measurements are not perfectly equal.
   Consider that durations defined below may differ from what is specified in
   PJDL v5.0. This is done to accomodate machine's inner workings and
   effectively produce the specified timing.

   Arduino Duemilanove/UNO/Nano is used as timing master, or the machine used
   to test all new supported MCUs.

   Benchmarks can be executed using NetworkAnalysis and SpeedTest examples.

   MODE   1: 1.97kB/s - 15808Bd
   MODE   2: 2.21kB/s - 17696Bd
   MODE   3: 3.10kB/s - 24844Bd
   MODE   4: 3.34kB/s - 26755Bd
   MODE   5: 6.3kB/s (data bit 12us, padding bit 50us, SWBB_PIN only)
   MODE   6: 8.2kB/s (data bit 8us, padding bit 50us, SWBB_PIN only)

   Use the same pin number on all connected devices to achieve maximum
   timing efficiency, not all different pin combinations work nominally
   because of execution timing discrepancies between physical pins. */

#pragma once

/* ATmega88/168/328 - Arduino Duemilanove, Uno, Nano, Mini, Pro, Pro mini */
#if defined(__AVR_ATmega88__)  || defined(__AVR_ATmega168__) || \
    defined(__AVR_ATmega328__) || defined(__AVR_ATmega328P__)
  #if SWBB_MODE == 1
    #if F_CPU == 16000000L
      /* Working on pin: 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, A0, A1 */
      #define SWBB_BIT_WIDTH   40
      #define SWBB_BIT_SPACER 112
      #define SWBB_ACCEPTANCE  56
      #define SWBB_READ_DELAY   4
    #endif
  #endif
  #if SWBB_MODE == 2
    #if F_CPU == 16000000L
      /* Working on pin: 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, A0, A1 */
      #define SWBB_BIT_WIDTH   36
      #define SWBB_BIT_SPACER  88
      #define SWBB_ACCEPTANCE  56
      #define SWBB_READ_DELAY   4
    #endif
  #endif
  #if SWBB_MODE == 3
    #if F_CPU == 16000000L
      /* Working on pin: 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, A0, A1 */
      #define SWBB_BIT_WIDTH   24
      #define SWBB_BIT_SPACER  66
      #define SWBB_ACCEPTANCE  30
      #define SWBB_READ_DELAY   8
    #endif
  #endif
  #if SWBB_MODE == 4
    #if F_CPU == 16000000L
      /* Working on pin: 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, A0, A1 */
      #define SWBB_BIT_WIDTH   22
      #define SWBB_BIT_SPACER  61
      #define SWBB_ACCEPTANCE  30
      #define SWBB_READ_DELAY   7
    #endif
  #endif
#endif

/* ATmega328PB ------------------------------------------------------------ */
#if defined(__AVR_ATmega328PB__)
  #if SWBB_MODE == 1
    #if F_CPU == 16000000L
      /* Working on pin: 10 */
      #define SWBB_BIT_WIDTH   37
      #define SWBB_BIT_SPACER 110
      #define SWBB_ACCEPTANCE  56
      #define SWBB_READ_DELAY   4
    #endif
  #endif
  #if SWBB_MODE == 2
    #if F_CPU == 16000000L
      /* Working on pin: 10 */
      #define SWBB_BIT_WIDTH   33
      #define SWBB_BIT_SPACER  88
      #define SWBB_ACCEPTANCE  56
      #define SWBB_READ_DELAY   8
    #endif
  #endif
#endif


/* ATmega16/32U4 - Arduino Leonardo/Micro --------------------------------- */
#if defined(__AVR_ATmega16U4__) || defined(__AVR_ATmega32U4__)
  #if SWBB_MODE == 1
    /* Working on pin: 2, 4, 8, 12
       Fallback to default timing */
    #define SWBB_BIT_WIDTH   40
    #define SWBB_BIT_SPACER 112
    #define SWBB_ACCEPTANCE  56
    #define SWBB_READ_DELAY   8
  #endif
  #if SWBB_MODE == 2
    /* Working on pin: 2, 4, 8, 12
       Fallback to default timing */
    #define SWBB_BIT_WIDTH   36
    #define SWBB_BIT_SPACER  88
    #define SWBB_ACCEPTANCE  56
    #define SWBB_READ_DELAY  12
  #endif
#endif

/* ATmega1280/2560 - Arduino Mega/Mega-nano ------------------------------- */
#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  #if SWBB_MODE == 1
    /* Working on pin: 3, 4, 7, 8, 9, 10, 12 */
    #define SWBB_BIT_WIDTH   38
    #define SWBB_BIT_SPACER 110
    #define SWBB_ACCEPTANCE  62
    #define SWBB_READ_DELAY  11
  #endif
  #if SWBB_MODE == 2
    /* Working on pin: 3, 4, 7, 8, 9, 10, 12 */
    #define SWBB_BIT_WIDTH   34
    #define SWBB_BIT_SPACER  86
    #define SWBB_ACCEPTANCE  58
    #define SWBB_READ_DELAY  10
  #endif
#endif

/* ATtiny45/85 ------------------------------------------------------------ */
#if defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
  #if SWBB_MODE == 1
    #if F_CPU == 16000000L
      /* Working on pin: 1, 2
         Fallback to default */
      #define SWBB_BIT_WIDTH   40
      #define SWBB_BIT_SPACER 112
      #define SWBB_ACCEPTANCE  56
      #define SWBB_READ_DELAY   4
    #endif
  #endif
  #if SWBB_MODE == 2
    #if F_CPU == 16000000L
      /* Working on pin: 1, 2
         Fallback to default */
      #define SWBB_BIT_WIDTH   36
      #define SWBB_BIT_SPACER  88
      #define SWBB_ACCEPTANCE  56
      #define SWBB_READ_DELAY   4
    #endif
  #endif
#endif

/* ATtiny44/84/44A/84A ---------------------------------------------------- */
#if defined(__AVR_ATtiny44__)  || defined(__AVR_ATtiny84__) || \
    defined(__AVR_ATtiny84A__) || defined(__AVR_ATtiny84A__)
  #if SWBB_MODE == 1
    #if F_CPU == 16000000L
      /* Working on pin: 0, 1, 2, 3, 4
         Fallback to default */
      #define SWBB_BIT_WIDTH   40
      #define SWBB_BIT_SPACER 112
      #define SWBB_ACCEPTANCE  56
      #define SWBB_READ_DELAY   4
    #endif
  #endif
  #if SWBB_MODE == 2
    #if F_CPU == 16000000L
      /* Working on pin: 0, 1, 2, 3, 4
         Fallback to default */
      #define SWBB_BIT_WIDTH   36
      #define SWBB_BIT_SPACER  88
      #define SWBB_ACCEPTANCE  56
      #define SWBB_READ_DELAY   4
    #endif
  #endif
#endif

/* Arduino Zero ----------------------------------------------------------- */
#if defined(ARDUINO_SAMD_ZERO)
  #if SWBB_MODE == 1
  /* Added by Esben Soeltoft - 03/09/2016
     Updated by Giovanni Blu Mitolo - 31/05/2019
     Working on pin: D0, D1, D3, A0, A1 */
    #define SWBB_BIT_WIDTH   43
    #define SWBB_BIT_SPACER 115
    #define SWBB_ACCEPTANCE  40
    #define SWBB_READ_DELAY  -4
  #endif
#endif

/* NodeMCU, generic ESP8266 ----------------------------------------------- */
#if defined(ESP8266)
  #if SWBB_MODE == 1
  /* Added by github user 240974a                 - 09/03/2016
     Added full support to MODE 1 (80 and 160MHz) - 12/06/2018 */
    #if (F_CPU == 80000000L) || (F_CPU == 160000000L)
      /* Working on pin: D1 or GPIO 5 */
      #define SWBB_BIT_WIDTH   44
      #define SWBB_BIT_SPACER 112
      #define SWBB_ACCEPTANCE  56
      #define SWBB_READ_DELAY  -6
    #endif
  #endif
#endif

/* Heltech WiFi LoRa ESP32, generic ESP32 --------------------------------- */
#if defined(ESP32)
  #if SWBB_MODE == 1
      /* Added full support to MODE 1 - 28/06/2018
         Working on pin: 12 and 25 */
      #define SWBB_BIT_WIDTH   44
      #define SWBB_BIT_SPACER 112
      #define SWBB_ACCEPTANCE  56
      #define SWBB_READ_DELAY  -2
    #endif
#endif

/* MK20DX256 - Teensy ----------------------------------------------------- */
#if defined(__MK20DX256__)
  #if SWBB_MODE == 1
  /* Added by github user SticilFace - 25/04/2016  */
    #if F_CPU == 96000000L
      #define SWBB_BIT_WIDTH   46
      #define SWBB_BIT_SPACER 112
      #define SWBB_ACCEPTANCE  40
      #define SWBB_READ_DELAY -10
    #endif
  #endif
#endif


/* STM32F1 ---------------------------------------------------------------- */
/* @jcallano 09-jul-2020 tested with pin PB15, PB14, PB13, PB12, PB11, PB10,
   PB9, PB8, PB7, PB6, PB4, PB3, PA15, PA10. 5v tolerant pins on bluepill */

#if defined(__STM32F1__)
  #if SWBB_MODE == 1
    #if F_CPU == 72000000L
      #define SWBB_BIT_WIDTH   43
      #define SWBB_BIT_SPACER 115
      #define SWBB_ACCEPTANCE  60
      #define SWBB_READ_DELAY 3
    #endif
  #endif
  #if SWBB_MODE == 2
    #if F_CPU == 72000000L
      #define SWBB_BIT_WIDTH   39
      #define SWBB_BIT_SPACER  91
      #define SWBB_ACCEPTANCE  47
      #define SWBB_READ_DELAY  3
    #endif
  #endif
  #if SWBB_MODE == 3
    #if F_CPU == 72000000L
      #define SWBB_BIT_WIDTH   27.5
      #define SWBB_BIT_SPACER  69.5
      #define SWBB_ACCEPTANCE  33
      #define SWBB_READ_DELAY  -5
    #endif
  #endif
  #if SWBB_MODE == 4
    #if F_CPU == 72000000L
      #define SWBB_BIT_WIDTH   25
      #define SWBB_BIT_SPACER  59
      #define SWBB_ACCEPTANCE  30 //
      #define SWBB_READ_DELAY  4
    #endif
  #endif
#endif

/* Modes 5 and 6 (faster than PJDL v5.0, not part of the specification) are
   supported only by the direct port access implementation (SWBB_PIN) and by
   the bootloader: the portable implementation can not produce or sample
   such short bits with digitalRead and digitalWrite. All devices of the bus
   must use the same mode. The padding bit is not shortened in proportion to
   the data bit: between bytes the receiver serves pending interrupts and
   PJON processes the received byte, the padding bit must still be high when
   it is ready (50us is enough with a 15us interrupt handler every 256us).
   The bus range is shorter than in mode 4, it was not measured: the values
   were chosen in simulation, which does not model the cable (see
   REVIEW-swbb-fast-modes.md). */

#if (SWBB_MODE == 5) || (SWBB_MODE == 6)
  #ifndef SWBB_PIN
    #error "SWBB_MODE 5 and 6 require SWBB_PIN (direct port access)"
  #endif
  #if SWBB_MODE == 5
    #ifndef SWBB_MODE5_PAD
      #define SWBB_MODE5_PAD   50
      #define SWBB_MODE5_BIT   12
      #define SWBB_MODE5_ACC   16
    #endif
    #define SWBB_BIT_WIDTH     SWBB_MODE5_BIT
    #define SWBB_BIT_SPACER    SWBB_MODE5_PAD
    #define SWBB_ACCEPTANCE    SWBB_MODE5_ACC
  #else
    #ifndef SWBB_MODE6_PAD
      #define SWBB_MODE6_PAD   50
      #define SWBB_MODE6_BIT    8
      #define SWBB_MODE6_ACC   12
    #endif
    #define SWBB_BIT_WIDTH     SWBB_MODE6_BIT
    #define SWBB_BIT_SPACER    SWBB_MODE6_PAD
    #define SWBB_ACCEPTANCE    SWBB_MODE6_ACC
  #endif
  #define SWBB_READ_DELAY      0
  #ifndef SWBB_LATENCY
    #define SWBB_LATENCY       5
  #endif
#endif

/* Avoid error if any previous defined ------------------------------------ */
#if SWBB_MODE == 1
  #ifndef SWBB_BIT_WIDTH
    #define SWBB_BIT_WIDTH   40
  #endif
  #ifndef SWBB_BIT_SPACER
    #define SWBB_BIT_SPACER 112
  #endif
  #ifndef SWBB_ACCEPTANCE
    #define SWBB_ACCEPTANCE  56
  #endif
  #ifndef SWBB_READ_DELAY
    #define SWBB_READ_DELAY   4
  #endif
  #ifndef SWBB_LATENCY
    #define SWBB_LATENCY     13
  #endif
#endif
#if SWBB_MODE == 2
  #ifndef SWBB_BIT_WIDTH
    #define SWBB_BIT_WIDTH   36
  #endif
  #ifndef SWBB_BIT_SPACER
    #define SWBB_BIT_SPACER  88
  #endif
  #ifndef SWBB_ACCEPTANCE
    #define SWBB_ACCEPTANCE  56
  #endif
  #ifndef SWBB_READ_DELAY
    #define SWBB_READ_DELAY   4
  #endif
  #ifndef SWBB_LATENCY
    #define SWBB_LATENCY     10
  #endif
#endif
#if SWBB_MODE == 3
  #ifndef SWBB_BIT_WIDTH
    #define SWBB_BIT_WIDTH   24
  #endif
  #ifndef SWBB_BIT_SPACER
    #define SWBB_BIT_SPACER  66
  #endif
  #ifndef SWBB_ACCEPTANCE
    #define SWBB_ACCEPTANCE  30
  #endif
  #ifndef SWBB_READ_DELAY
    #define SWBB_READ_DELAY   8
  #endif
  #ifndef SWBB_LATENCY
    #define SWBB_LATENCY      8
  #endif
#endif
#if SWBB_MODE == 4
  #ifndef SWBB_BIT_WIDTH
    #define SWBB_BIT_WIDTH   22
  #endif
  #ifndef SWBB_BIT_SPACER
    #define SWBB_BIT_SPACER  61
  #endif
  #ifndef SWBB_ACCEPTANCE
    #define SWBB_ACCEPTANCE  30
  #endif
  #ifndef SWBB_READ_DELAY
    #define SWBB_READ_DELAY   7
  #endif
  #ifndef SWBB_LATENCY
    #define SWBB_LATENCY      5
  #endif
#endif


/* PJDL v5.0 timing as it must appear on the wire, in microseconds. Used by
   the direct port access implementation (SWBB_PIN defined), which executes
   in a known number of cycles and so does not need the per-MCU adjustments
   above. The bootloader uses the same padding and data bit values. The keep
   busy bit is shorter than specified in modes 1 and 2 (still within its -5us
   tolerance): receivers of previous versions wait for its falling edge for
   only SWBB_BIT_WIDTH / 4 and would otherwise start the response while it
   is still being transmitted. */

#if SWBB_MODE == 1
  #define SWBB_WIRE_PAD_DEFAULT        110
  #define SWBB_WIRE_BIT_DEFAULT         44
  #define SWBB_WIRE_KEEP_BUSY_DEFAULT    9
#elif SWBB_MODE == 2
  #define SWBB_WIRE_PAD_DEFAULT         92
  #define SWBB_WIRE_BIT_DEFAULT         40
  #define SWBB_WIRE_KEEP_BUSY_DEFAULT    8
#elif SWBB_MODE == 3
  #define SWBB_WIRE_PAD_DEFAULT         70
  #define SWBB_WIRE_BIT_DEFAULT         28
  #define SWBB_WIRE_KEEP_BUSY_DEFAULT    7
#elif SWBB_MODE == 4
  #define SWBB_WIRE_PAD_DEFAULT         65
  #define SWBB_WIRE_BIT_DEFAULT         26
  #define SWBB_WIRE_KEEP_BUSY_DEFAULT  6.5
#elif (SWBB_MODE == 5) || (SWBB_MODE == 6)
  #define SWBB_WIRE_PAD_DEFAULT       SWBB_BIT_SPACER
  #define SWBB_WIRE_BIT_DEFAULT       SWBB_BIT_WIDTH
  #define SWBB_WIRE_KEEP_BUSY_DEFAULT    4
#endif

#ifndef SWBB_WIRE_PAD
  #define SWBB_WIRE_PAD SWBB_WIRE_PAD_DEFAULT
#endif
#ifndef SWBB_WIRE_BIT
  #define SWBB_WIRE_BIT SWBB_WIRE_BIT_DEFAULT
#endif
#ifndef SWBB_WIRE_KEEP_BUSY
  #define SWBB_WIRE_KEEP_BUSY SWBB_WIRE_KEEP_BUSY_DEFAULT
#endif

/* Data bit duration transmitted and expected when receiving by the direct
   port access implementation. Receivers using digitalRead (the portable
   implementation) sample with a period slightly longer than
   SWBB_WIRE_BIT (in mode 3 they accept about 28.2-29.8us), receivers with
   exact timing (the bootloader) accept about 27-29.5us: transmitting
   SWBB_WIRE_BIT + 1 is within both. When receiving, the data bit is expected
   to last SWBB_WIRE_BIT + 0.5, in the middle between what the portable
   implementation (~SWBB_WIRE_BIT + 0.2), the bootloader (SWBB_WIRE_BIT) and
   this implementation (SWBB_WIRE_BIT + 1) transmit. */

#if SWBB_MODE >= 5
  /* Only exact implementations support modes 5 and 6: no offset */
  #ifndef SWBB_TX_BIT
    #define SWBB_TX_BIT SWBB_WIRE_BIT
  #endif
  #ifndef SWBB_RX_BIT
    #define SWBB_RX_BIT SWBB_WIRE_BIT
  #endif
#endif
/* Cycles executed between the detection of the falling edge of a padding
   bit and the sampling of the first data bit (the receiver samples this much
   later than the middle of each bit). Compensated only in modes 5 and 6,
   where it is a significant fraction of a bit (modes 1-4 were validated with
   it, their timing is unchanged). */
#ifndef SWBB_SAMPLE_ADVANCE
  #if SWBB_MODE >= 5
    #define SWBB_SAMPLE_ADVANCE 50
  #else
    #define SWBB_SAMPLE_ADVANCE 0
  #endif
#endif

#ifndef SWBB_TX_BIT
  #define SWBB_TX_BIT (SWBB_WIRE_BIT + 1)
#endif
#ifndef SWBB_RX_BIT
  #define SWBB_RX_BIT (SWBB_WIRE_BIT + 0.5)
#endif

/* Longest padding bit accepted by the direct port access implementation
   (PJDL tolerance is +17us in mode 1, the portable implementation transmits
   it a few microseconds longer if interrupted) */

#ifndef SWBB_WIRE_PAD_MAX
  #define SWBB_WIRE_PAD_MAX (SWBB_WIRE_PAD + 20)
#endif

/* Response: after the falling edge of the transmitter's keep busy bit wait
   SWBB_RESPONSE_DELAY microseconds before transmitting the response. A
   transmitter using digitalWrite and pinMode keeps driving the pin low for a
   few microseconds after the falling edge before releasing it, transmitting
   earlier would cause bus contention. SWBB_RESPONSE_PULSE_WAIT is the maximum
   time waited for a keep busy bit (the keep busy bit period is ~52us using
   digitalWrite on a 16MHz ATmega328P, ~48us with direct port access). */

#ifndef SWBB_RESPONSE_DELAY
  #define SWBB_RESPONSE_DELAY       18
#endif

/* Same delay for the portable implementation: detecting the falling edge
   with micros and digitalRead and switching the pin with digitalWrite and
   pinMode already take 9-18us on a 16MHz ATmega328P, so none is added */

#ifndef SWBB_RESPONSE_DELAY_PORTABLE
  #define SWBB_RESPONSE_DELAY_PORTABLE 0
#endif

#ifndef SWBB_RESPONSE_PULSE_WAIT
  #define SWBB_RESPONSE_PULSE_WAIT 100
#endif

/* Time the transmitter waits for a response after each keep busy bit before
   transmitting the next one (direct port access implementation only). It must
   be longer than SWBB_RESPONSE_DELAY plus the receiver's reaction time. */

#ifndef SWBB_RESPONSE_LISTEN
  #define SWBB_RESPONSE_LISTEN      40
#endif

/* Frame preamble length (by default set to 1 x SWBB_BIT_SPACER)
   Maximum allowed value is 100 or a preamble of 100 x SWBB_BIT_SPACER
   When devices execute other tasks frames may be lost unheard. The preamble
   can be used to avoid transmission failure. To avoid retransmissions the
   preamble length should be slightly longer than the task's duration. */

#ifndef SWBB_PREAMBLE
  #define SWBB_PREAMBLE 1
#endif

/* Maximum frame preamble length (by default set to 1 x SWBB_BIT_SPACER)
   Maximum allowed value is 100 or a preamble of 100 x SWBB_BIT_SPACER
   To have full interoperability set SWBB_MAX_PREAMBLE = 100 */

#ifndef SWBB_MAX_PREAMBLE
  #define SWBB_MAX_PREAMBLE 1
#endif

/* Synchronous acknowledgement response offset.
   If (latency + CRC computation) > (SWBB_RESPONSE_OFFSET * length)
   synchronous acknowledgement reliability could be affected or disrupted
   set a higher SWBB_RESPONSE_OFFSET if necessary. */

#ifndef SWBB_RESPONSE_OFFSET
  #define SWBB_RESPONSE_OFFSET 20
#endif

/* Maximum initial delay in milliseconds: */

#ifndef SWBB_INITIAL_DELAY
  #define SWBB_INITIAL_DELAY 1000
#endif

/* Maximum delay in case of collision in microseconds: */

#ifndef SWBB_COLLISION_DELAY
  #define SWBB_COLLISION_DELAY 16
#endif

/* Maximum transmission attempts */

#ifndef SWBB_MAX_ATTEMPTS
  #define SWBB_MAX_ATTEMPTS    20
#endif

/* Back-off exponential degree */

#ifndef SWBB_BACK_OFF_DEGREE
  #define SWBB_BACK_OFF_DEGREE  4
#endif
