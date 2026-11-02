/* SoftwareBitBang direct port access: pin mapping (Arduino pin numbers)

   SWBB_PIN -> SWBB_PORT, SWBB_DDR, SWBB_IN (port registers, in the I/O space
   reachable by sbi, cbi, sbis and sbic) and SWBB_BIT. If the pin has an
   external interrupt (used by SWBB_INTERRUPT_WAKE, SWBB_INTERRUPT and the
   mode 7 wake receiver): SWBB_EXT_INT_VECT, SWBB_EXT_INT_ENABLE (EIMSK bit),
   SWBB_EXT_INT_FLAG (EIFR bit), SWBB_EXT_INT_EICR (EICRA or EICRB) and
   SWBB_EXT_INT_ISC0 (position of the ISCn0 bit in it).

   ATmega88/168/328/328PB (Uno, Nano, Pro Mini): pins 0-19, external
   interrupts on 2 (INT0) and 3 (INT1).
   ATmega1280/2560 (Mega): pins of ports A-G, external interrupts on 2 (INT4),
   3 (INT5), 18 (INT3), 19 (INT2), 20 (INT1) and 21 (INT0). */

#pragma once

#if defined(__AVR_ATmega88__)  || defined(__AVR_ATmega88P__)  || \
    defined(__AVR_ATmega168__) || defined(__AVR_ATmega168P__) || \
    defined(__AVR_ATmega328__) || defined(__AVR_ATmega328P__) || \
    defined(__AVR_ATmega328PB__)
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
  #if SWBB_PIN == 2
    #define SWBB_EXT_INT_VECT   INT0_vect
    #define SWBB_EXT_INT_ENABLE (1 << INT0)
    #define SWBB_EXT_INT_FLAG   (1 << INTF0)
    #define SWBB_EXT_INT_EICR   EICRA
    #define SWBB_EXT_INT_ISC0   0
  #elif SWBB_PIN == 3
    #define SWBB_EXT_INT_VECT   INT1_vect
    #define SWBB_EXT_INT_ENABLE (1 << INT1)
    #define SWBB_EXT_INT_FLAG   (1 << INTF1)
    #define SWBB_EXT_INT_EICR   EICRA
    #define SWBB_EXT_INT_ISC0   2
  #endif
#elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  /* Arduino Mega variant numbering. The cycle counted code has no calls
     (the 3 byte return address of these MCUs adds 1 cycle to each call,
     return and interrupt entry: only the interrupt latency changes) */
  #if SWBB_PIN == 0
    #define SWBB_PORT PORTE
    #define SWBB_DDR  DDRE
    #define SWBB_IN   PINE
    #define SWBB_BIT  0
  #elif SWBB_PIN == 1
    #define SWBB_PORT PORTE
    #define SWBB_DDR  DDRE
    #define SWBB_IN   PINE
    #define SWBB_BIT  1
  #elif SWBB_PIN == 2
    #define SWBB_PORT PORTE
    #define SWBB_DDR  DDRE
    #define SWBB_IN   PINE
    #define SWBB_BIT  4
    #define SWBB_EXT_INT_VECT   INT4_vect
    #define SWBB_EXT_INT_ENABLE (1 << INT4)
    #define SWBB_EXT_INT_FLAG   (1 << INTF4)
    #define SWBB_EXT_INT_EICR   EICRB
    #define SWBB_EXT_INT_ISC0   0
  #elif SWBB_PIN == 3
    #define SWBB_PORT PORTE
    #define SWBB_DDR  DDRE
    #define SWBB_IN   PINE
    #define SWBB_BIT  5
    #define SWBB_EXT_INT_VECT   INT5_vect
    #define SWBB_EXT_INT_ENABLE (1 << INT5)
    #define SWBB_EXT_INT_FLAG   (1 << INTF5)
    #define SWBB_EXT_INT_EICR   EICRB
    #define SWBB_EXT_INT_ISC0   2
  #elif SWBB_PIN == 4
    #define SWBB_PORT PORTG
    #define SWBB_DDR  DDRG
    #define SWBB_IN   PING
    #define SWBB_BIT  5
  #elif SWBB_PIN == 5
    #define SWBB_PORT PORTE
    #define SWBB_DDR  DDRE
    #define SWBB_IN   PINE
    #define SWBB_BIT  3
  #elif SWBB_PIN == 10
    #define SWBB_PORT PORTB
    #define SWBB_DDR  DDRB
    #define SWBB_IN   PINB
    #define SWBB_BIT  4
  #elif SWBB_PIN == 11
    #define SWBB_PORT PORTB
    #define SWBB_DDR  DDRB
    #define SWBB_IN   PINB
    #define SWBB_BIT  5
  #elif SWBB_PIN == 12
    #define SWBB_PORT PORTB
    #define SWBB_DDR  DDRB
    #define SWBB_IN   PINB
    #define SWBB_BIT  6
  #elif SWBB_PIN == 13
    #define SWBB_PORT PORTB
    #define SWBB_DDR  DDRB
    #define SWBB_IN   PINB
    #define SWBB_BIT  7
  #elif SWBB_PIN == 18
    #define SWBB_PORT PORTD
    #define SWBB_DDR  DDRD
    #define SWBB_IN   PIND
    #define SWBB_BIT  3
    #define SWBB_EXT_INT_VECT   INT3_vect
    #define SWBB_EXT_INT_ENABLE (1 << INT3)
    #define SWBB_EXT_INT_FLAG   (1 << INTF3)
    #define SWBB_EXT_INT_EICR   EICRA
    #define SWBB_EXT_INT_ISC0   6
  #elif SWBB_PIN == 19
    #define SWBB_PORT PORTD
    #define SWBB_DDR  DDRD
    #define SWBB_IN   PIND
    #define SWBB_BIT  2
    #define SWBB_EXT_INT_VECT   INT2_vect
    #define SWBB_EXT_INT_ENABLE (1 << INT2)
    #define SWBB_EXT_INT_FLAG   (1 << INTF2)
    #define SWBB_EXT_INT_EICR   EICRA
    #define SWBB_EXT_INT_ISC0   4
  #elif SWBB_PIN == 20
    #define SWBB_PORT PORTD
    #define SWBB_DDR  DDRD
    #define SWBB_IN   PIND
    #define SWBB_BIT  1
    #define SWBB_EXT_INT_VECT   INT1_vect
    #define SWBB_EXT_INT_ENABLE (1 << INT1)
    #define SWBB_EXT_INT_FLAG   (1 << INTF1)
    #define SWBB_EXT_INT_EICR   EICRA
    #define SWBB_EXT_INT_ISC0   2
  #elif SWBB_PIN == 21
    #define SWBB_PORT PORTD
    #define SWBB_DDR  DDRD
    #define SWBB_IN   PIND
    #define SWBB_BIT  0
    #define SWBB_EXT_INT_VECT   INT0_vect
    #define SWBB_EXT_INT_ENABLE (1 << INT0)
    #define SWBB_EXT_INT_FLAG   (1 << INTF0)
    #define SWBB_EXT_INT_EICR   EICRA
    #define SWBB_EXT_INT_ISC0   0
  #elif SWBB_PIN == 22
    #define SWBB_PORT PORTA
    #define SWBB_DDR  DDRA
    #define SWBB_IN   PINA
    #define SWBB_BIT  0
  #elif SWBB_PIN == 23
    #define SWBB_PORT PORTA
    #define SWBB_DDR  DDRA
    #define SWBB_IN   PINA
    #define SWBB_BIT  1
  #elif SWBB_PIN == 24
    #define SWBB_PORT PORTA
    #define SWBB_DDR  DDRA
    #define SWBB_IN   PINA
    #define SWBB_BIT  2
  #elif SWBB_PIN == 25
    #define SWBB_PORT PORTA
    #define SWBB_DDR  DDRA
    #define SWBB_IN   PINA
    #define SWBB_BIT  3
  #elif SWBB_PIN == 26
    #define SWBB_PORT PORTA
    #define SWBB_DDR  DDRA
    #define SWBB_IN   PINA
    #define SWBB_BIT  4
  #elif SWBB_PIN == 27
    #define SWBB_PORT PORTA
    #define SWBB_DDR  DDRA
    #define SWBB_IN   PINA
    #define SWBB_BIT  5
  #elif SWBB_PIN == 28
    #define SWBB_PORT PORTA
    #define SWBB_DDR  DDRA
    #define SWBB_IN   PINA
    #define SWBB_BIT  6
  #elif SWBB_PIN == 29
    #define SWBB_PORT PORTA
    #define SWBB_DDR  DDRA
    #define SWBB_IN   PINA
    #define SWBB_BIT  7
  #elif SWBB_PIN == 30
    #define SWBB_PORT PORTC
    #define SWBB_DDR  DDRC
    #define SWBB_IN   PINC
    #define SWBB_BIT  7
  #elif SWBB_PIN == 31
    #define SWBB_PORT PORTC
    #define SWBB_DDR  DDRC
    #define SWBB_IN   PINC
    #define SWBB_BIT  6
  #elif SWBB_PIN == 32
    #define SWBB_PORT PORTC
    #define SWBB_DDR  DDRC
    #define SWBB_IN   PINC
    #define SWBB_BIT  5
  #elif SWBB_PIN == 33
    #define SWBB_PORT PORTC
    #define SWBB_DDR  DDRC
    #define SWBB_IN   PINC
    #define SWBB_BIT  4
  #elif SWBB_PIN == 34
    #define SWBB_PORT PORTC
    #define SWBB_DDR  DDRC
    #define SWBB_IN   PINC
    #define SWBB_BIT  3
  #elif SWBB_PIN == 35
    #define SWBB_PORT PORTC
    #define SWBB_DDR  DDRC
    #define SWBB_IN   PINC
    #define SWBB_BIT  2
  #elif SWBB_PIN == 36
    #define SWBB_PORT PORTC
    #define SWBB_DDR  DDRC
    #define SWBB_IN   PINC
    #define SWBB_BIT  1
  #elif SWBB_PIN == 37
    #define SWBB_PORT PORTC
    #define SWBB_DDR  DDRC
    #define SWBB_IN   PINC
    #define SWBB_BIT  0
  #elif SWBB_PIN == 38
    #define SWBB_PORT PORTD
    #define SWBB_DDR  DDRD
    #define SWBB_IN   PIND
    #define SWBB_BIT  7
  #elif SWBB_PIN == 39
    #define SWBB_PORT PORTG
    #define SWBB_DDR  DDRG
    #define SWBB_IN   PING
    #define SWBB_BIT  2
  #elif SWBB_PIN == 40
    #define SWBB_PORT PORTG
    #define SWBB_DDR  DDRG
    #define SWBB_IN   PING
    #define SWBB_BIT  1
  #elif SWBB_PIN == 41
    #define SWBB_PORT PORTG
    #define SWBB_DDR  DDRG
    #define SWBB_IN   PING
    #define SWBB_BIT  0
  #elif SWBB_PIN == 50
    #define SWBB_PORT PORTB
    #define SWBB_DDR  DDRB
    #define SWBB_IN   PINB
    #define SWBB_BIT  3
  #elif SWBB_PIN == 51
    #define SWBB_PORT PORTB
    #define SWBB_DDR  DDRB
    #define SWBB_IN   PINB
    #define SWBB_BIT  2
  #elif SWBB_PIN == 52
    #define SWBB_PORT PORTB
    #define SWBB_DDR  DDRB
    #define SWBB_IN   PINB
    #define SWBB_BIT  1
  #elif SWBB_PIN == 53
    #define SWBB_PORT PORTB
    #define SWBB_DDR  DDRB
    #define SWBB_IN   PINB
    #define SWBB_BIT  0
  #elif SWBB_PIN == 54
    #define SWBB_PORT PORTF
    #define SWBB_DDR  DDRF
    #define SWBB_IN   PINF
    #define SWBB_BIT  0
  #elif SWBB_PIN == 55
    #define SWBB_PORT PORTF
    #define SWBB_DDR  DDRF
    #define SWBB_IN   PINF
    #define SWBB_BIT  1
  #elif SWBB_PIN == 56
    #define SWBB_PORT PORTF
    #define SWBB_DDR  DDRF
    #define SWBB_IN   PINF
    #define SWBB_BIT  2
  #elif SWBB_PIN == 57
    #define SWBB_PORT PORTF
    #define SWBB_DDR  DDRF
    #define SWBB_IN   PINF
    #define SWBB_BIT  3
  #elif SWBB_PIN == 58
    #define SWBB_PORT PORTF
    #define SWBB_DDR  DDRF
    #define SWBB_IN   PINF
    #define SWBB_BIT  4
  #elif SWBB_PIN == 59
    #define SWBB_PORT PORTF
    #define SWBB_DDR  DDRF
    #define SWBB_IN   PINF
    #define SWBB_BIT  5
  #elif SWBB_PIN == 60
    #define SWBB_PORT PORTF
    #define SWBB_DDR  DDRF
    #define SWBB_IN   PINF
    #define SWBB_BIT  6
  #elif SWBB_PIN == 61
    #define SWBB_PORT PORTF
    #define SWBB_DDR  DDRF
    #define SWBB_IN   PINF
    #define SWBB_BIT  7
  #else
    #error "SWBB_PIN: on the Mega only pins of ports A-G can be used (not 6-9, 14-17, 42-49, 62-69: ports H, J, K, L are out of reach of sbi, cbi and sbis)"
  #endif
#else
  #error "SWBB_PIN (direct port access) supports only ATmega88/168/328/328PB and ATmega1280/2560"
#endif

/* Sense control of the external interrupt (2 bits in EICRA or EICRB) */
#ifdef SWBB_EXT_INT_VECT
  #define SWBB_EXT_INT_SENSE_MASK   (3 << SWBB_EXT_INT_ISC0)
  #define SWBB_EXT_INT_SENSE_RISING (3 << SWBB_EXT_INT_ISC0)
  #define SWBB_EXT_INT_SENSE_CHANGE (1 << SWBB_EXT_INT_ISC0)
#endif
