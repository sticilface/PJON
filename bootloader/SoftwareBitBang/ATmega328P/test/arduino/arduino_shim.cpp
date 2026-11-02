/* Minimal Arduino core replacement used only by the bootloader simulation
   test (see Arduino.h). Written for the ATmega328P at 16MHz. */

#include "Arduino.h"

/* Timer 0: millis / micros ------------------------------------------------ */

/* Timer 0 runs at F_CPU / 64 and overflows every 1024us, like Arduino */
static volatile unsigned long timer0_overflow_count = 0;
static volatile unsigned long timer0_millis = 0;
static volatile uint8_t timer0_fract = 0;

ISR(TIMER0_OVF_vect) {
  unsigned long m = timer0_millis;
  uint8_t f = timer0_fract;
  m += 1;
  f += 3;
  if(f >= 125) {
    f -= 125;
    m += 1;
  }
  timer0_fract = f;
  timer0_millis = m;
  timer0_overflow_count++;
}

unsigned long millis(void) {
  uint8_t old_sreg = SREG;
  cli();
  unsigned long m = timer0_millis;
  SREG = old_sreg;
  return m;
}

unsigned long micros(void) {
  uint8_t old_sreg = SREG, t;
  cli();
  unsigned long m = timer0_overflow_count;
  t = TCNT0;
  if((TIFR0 & _BV(TOV0)) && (t < 255)) m++;
  SREG = old_sreg;
  return ((m << 8) + t) * 4;
}

void delay(unsigned long ms) {
  uint32_t start = micros();
  while(ms > 0) {
    while(ms > 0 && (micros() - start) >= 1000) {
      ms--;
      start += 1000;
    }
  }
}

/* Busy wait loop of 4 cycles per iteration, as in the Arduino core */
void delayMicroseconds(unsigned int us) {
  if(us <= 1) return;
  us <<= 2;
  us -= 5;
  __asm__ __volatile__ (
    "1: sbiw %0,1" "\n\t"
    "brne 1b" : "=w" (us) : "0" (us)
  );
}

/* Digital IO (lookup tables in flash, as in the Arduino core) ------------- */

/* Timer numbering of the Arduino AVR core (Arduino.h) */
enum {
  NOT_ON_TIMER, TIMER0A, TIMER0B, TIMER1A, TIMER1B, TIMER1C, TIMER2, TIMER2A,
  TIMER2B, TIMER3A, TIMER3B, TIMER3C, TIMER4A, TIMER4B, TIMER4C, TIMER4D,
  TIMER5A, TIMER5B, TIMER5C
};

#if defined(__AVR_ATmega2560__) || defined(__AVR_ATmega1280__)
/* Arduino Mega variant (variants/mega/pins_arduino.h) */
#define PA 1
#define PB 2
#define PC 3
#define PD 4
#define PE 5
#define PF 6
#define PG 7
#define PH 8
#define PJ 10
#define PK 11
#define PL 12
#define PIN_COUNT 70

static const uint8_t PROGMEM pin_to_port[] = {
  PE, PE, PE, PE, PG, PE, PH, PH, PH, PH, PB, PB, PB, PB, PJ, PJ, PH, PH,
  PD, PD, PD, PD, PA, PA, PA, PA, PA, PA, PA, PA, PC, PC, PC, PC, PC, PC,
  PC, PC, PD, PG, PG, PG, PL, PL, PL, PL, PL, PL, PL, PL, PB, PB, PB, PB,
  PF, PF, PF, PF, PF, PF, PF, PF, PK, PK, PK, PK, PK, PK, PK, PK
};

static const uint8_t PROGMEM pin_to_mask[] = {
  _BV(0), _BV(1), _BV(4), _BV(5), _BV(5), _BV(3), _BV(3), _BV(4), _BV(5),
  _BV(6), _BV(4), _BV(5), _BV(6), _BV(7), _BV(1), _BV(0), _BV(1), _BV(0),
  _BV(3), _BV(2), _BV(1), _BV(0), _BV(0), _BV(1), _BV(2), _BV(3), _BV(4),
  _BV(5), _BV(6), _BV(7), _BV(7), _BV(6), _BV(5), _BV(4), _BV(3), _BV(2),
  _BV(1), _BV(0), _BV(7), _BV(2), _BV(1), _BV(0), _BV(7), _BV(6), _BV(5),
  _BV(4), _BV(3), _BV(2), _BV(1), _BV(0), _BV(3), _BV(2), _BV(1), _BV(0),
  _BV(0), _BV(1), _BV(2), _BV(3), _BV(4), _BV(5), _BV(6), _BV(7),
  _BV(0), _BV(1), _BV(2), _BV(3), _BV(4), _BV(5), _BV(6), _BV(7)
};

static const uint8_t PROGMEM pin_to_timer[] = {
  NOT_ON_TIMER, NOT_ON_TIMER, TIMER3B, TIMER3C, TIMER0B, TIMER3A, TIMER4A,
  TIMER4B, TIMER4C, TIMER2B, TIMER2A, TIMER1A, TIMER1B, TIMER0A,
  NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER,
  NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER,
  NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER,
  NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER,
  NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER,
  NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER,
  TIMER5C, TIMER5B, TIMER5A, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER,
  NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER,
  NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER,
  NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER,
  NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER, NOT_ON_TIMER
};

static const uint16_t PROGMEM port_to_output[] = {
  0, (uint16_t)&PORTA, (uint16_t)&PORTB, (uint16_t)&PORTC, (uint16_t)&PORTD,
  (uint16_t)&PORTE, (uint16_t)&PORTF, (uint16_t)&PORTG, (uint16_t)&PORTH, 0,
  (uint16_t)&PORTJ, (uint16_t)&PORTK, (uint16_t)&PORTL
};
static const uint16_t PROGMEM port_to_mode[] = {
  0, (uint16_t)&DDRA, (uint16_t)&DDRB, (uint16_t)&DDRC, (uint16_t)&DDRD,
  (uint16_t)&DDRE, (uint16_t)&DDRF, (uint16_t)&DDRG, (uint16_t)&DDRH, 0,
  (uint16_t)&DDRJ, (uint16_t)&DDRK, (uint16_t)&DDRL
};
static const uint16_t PROGMEM port_to_input[] = {
  0, (uint16_t)&PINA, (uint16_t)&PINB, (uint16_t)&PINC, (uint16_t)&PIND,
  (uint16_t)&PINE, (uint16_t)&PINF, (uint16_t)&PING, (uint16_t)&PINH, 0,
  (uint16_t)&PINJ, (uint16_t)&PINK, (uint16_t)&PINL
};
#else
/* Arduino Uno variant (variants/standard/pins_arduino.h) */
#define PORT_B 2
#define PORT_C 3
#define PORT_D 4
#define PIN_COUNT 20

static const uint8_t PROGMEM pin_to_port[] = {
  PORT_D, PORT_D, PORT_D, PORT_D, PORT_D, PORT_D, PORT_D, PORT_D,
  PORT_B, PORT_B, PORT_B, PORT_B, PORT_B, PORT_B,
  PORT_C, PORT_C, PORT_C, PORT_C, PORT_C, PORT_C
};

static const uint8_t PROGMEM pin_to_mask[] = {
  _BV(0), _BV(1), _BV(2), _BV(3), _BV(4), _BV(5), _BV(6), _BV(7),
  _BV(0), _BV(1), _BV(2), _BV(3), _BV(4), _BV(5),
  _BV(0), _BV(1), _BV(2), _BV(3), _BV(4), _BV(5)
};

/* Kept as calibrated for the previous tests: no pin on a timer (the pins
   used by the tests, 2, 12 and 17, are not), turn_off_pwm is not called */
static const uint8_t PROGMEM pin_to_timer[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
#define SHIM_NO_PWM

static const uint16_t PROGMEM port_to_output[] = {
  0, 0, (uint16_t)&PORTB, (uint16_t)&PORTC, (uint16_t)&PORTD
};
static const uint16_t PROGMEM port_to_mode[] = {
  0, 0, (uint16_t)&DDRB, (uint16_t)&DDRC, (uint16_t)&DDRD
};
static const uint16_t PROGMEM port_to_input[] = {
  0, 0, (uint16_t)&PINB, (uint16_t)&PINC, (uint16_t)&PIND
};
#endif

/* As turnOffPWM in wiring_digital.c of the Arduino AVR core: called by
   digitalRead and digitalWrite for every pin connected to a timer output
   (for example pin 3 of the Mega, TIMER3C), its time is part of theirs */
#ifdef SHIM_NO_PWM
static void turn_off_pwm(uint8_t timer) { (void)timer; }
#else
static void turn_off_pwm(uint8_t timer) {
  switch(timer) {
    #if defined(TCCR1A) && defined(COM1A1)
    case TIMER1A: TCCR1A &= ~_BV(COM1A1); break;
    #endif
    #if defined(TCCR1A) && defined(COM1B1)
    case TIMER1B: TCCR1A &= ~_BV(COM1B1); break;
    #endif
    #if defined(TCCR1A) && defined(COM1C1)
    case TIMER1C: TCCR1A &= ~_BV(COM1C1); break;
    #endif
    #if defined(TCCR0A) && defined(COM0A1)
    case TIMER0A: TCCR0A &= ~_BV(COM0A1); break;
    #endif
    #if defined(TCCR0A) && defined(COM0B1)
    case TIMER0B: TCCR0A &= ~_BV(COM0B1); break;
    #endif
    #if defined(TCCR2A) && defined(COM2A1)
    case TIMER2A: TCCR2A &= ~_BV(COM2A1); break;
    #endif
    #if defined(TCCR2A) && defined(COM2B1)
    case TIMER2B: TCCR2A &= ~_BV(COM2B1); break;
    #endif
    #if defined(TCCR3A) && defined(COM3A1)
    case TIMER3A: TCCR3A &= ~_BV(COM3A1); break;
    #endif
    #if defined(TCCR3A) && defined(COM3B1)
    case TIMER3B: TCCR3A &= ~_BV(COM3B1); break;
    #endif
    #if defined(TCCR3A) && defined(COM3C1)
    case TIMER3C: TCCR3A &= ~_BV(COM3C1); break;
    #endif
    #if defined(TCCR4A) && defined(COM4A1)
    case TIMER4A: TCCR4A &= ~_BV(COM4A1); break;
    #endif
    #if defined(TCCR4A) && defined(COM4B1)
    case TIMER4B: TCCR4A &= ~_BV(COM4B1); break;
    #endif
    #if defined(TCCR4A) && defined(COM4C1)
    case TIMER4C: TCCR4A &= ~_BV(COM4C1); break;
    #endif
    #if defined(TCCR5A) && defined(COM5A1)
    case TIMER5A: TCCR5A &= ~_BV(COM5A1); break;
    case TIMER5B: TCCR5A &= ~_BV(COM5B1); break;
    case TIMER5C: TCCR5A &= ~_BV(COM5C1); break;
    #endif
    default: break;
  }
}
#endif

void pinMode(uint8_t pin, uint8_t mode) {
  uint8_t bit = pgm_read_byte(pin_to_mask + pin);
  uint8_t port = pgm_read_byte(pin_to_port + pin);
  volatile uint8_t *reg, *out;
  if(!port) return;
  reg = (volatile uint8_t *)pgm_read_word(port_to_mode + port);
  out = (volatile uint8_t *)pgm_read_word(port_to_output + port);
  uint8_t old_sreg = SREG;
  cli();
  if(mode == INPUT) {
    *reg &= ~bit;
    *out &= ~bit;
  } else if(mode == INPUT_PULLUP) {
    *reg &= ~bit;
    *out |= bit;
  } else *reg |= bit;
  SREG = old_sreg;
}

void digitalWrite(uint8_t pin, uint8_t value) {
  uint8_t timer = pgm_read_byte(pin_to_timer + pin);
  uint8_t bit = pgm_read_byte(pin_to_mask + pin);
  uint8_t port = pgm_read_byte(pin_to_port + pin);
  if(!port) return;
  if(timer != NOT_ON_TIMER) turn_off_pwm(timer);
  volatile uint8_t *out =
    (volatile uint8_t *)pgm_read_word(port_to_output + port);
  uint8_t old_sreg = SREG;
  cli();
  if(value == LOW) *out &= ~bit;
  else *out |= bit;
  SREG = old_sreg;
}

int digitalRead(uint8_t pin) {
  uint8_t timer = pgm_read_byte(pin_to_timer + pin);
  uint8_t bit = pgm_read_byte(pin_to_mask + pin);
  uint8_t port = pgm_read_byte(pin_to_port + pin);
  if(!port) return LOW;
  if(timer != NOT_ON_TIMER) turn_off_pwm(timer);
  if(*(volatile uint8_t *)pgm_read_word(port_to_input + port) & bit)
    return HIGH;
  return LOW;
}

int analogRead(uint8_t pin) {
  (void)pin;
  return (int)(TCNT0 ^ timer0_millis);
}

/* Random ------------------------------------------------------------------ */

void randomSeed(unsigned long seed) {
  if(seed != 0) srandom(seed);
}

long random(long howbig) {
  if(howbig == 0) return 0;
  return ::random() % howbig;
}

long random(long howsmall, long howbig) {
  if(howsmall >= howbig) return howsmall;
  return random(howbig - howsmall) + howsmall;
}

/* Serial (USART0, 115200 baud, interrupt driven reception) ---------------- */

static volatile uint8_t rx_buffer[64];
static volatile uint8_t rx_head = 0, rx_tail = 0;

#ifdef USART0_RX_vect
ISR(USART0_RX_vect) {
#else
ISR(USART_RX_vect) {
#endif
  uint8_t c = UDR0;
  uint8_t next = (rx_head + 1) % sizeof(rx_buffer);
  if(next != rx_tail) {
    rx_buffer[rx_head] = c;
    rx_head = next;
  }
}

SerialPort Serial;

void SerialPort::begin(unsigned long baud) {
  uint16_t setting = (F_CPU / 4 / baud - 1) / 2;
  UCSR0A = _BV(U2X0);
  UBRR0H = setting >> 8;
  UBRR0L = setting;
  UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
  UCSR0B = _BV(RXEN0) | _BV(TXEN0) | _BV(RXCIE0);
}

int SerialPort::available(void) {
  return (sizeof(rx_buffer) + rx_head - rx_tail) % sizeof(rx_buffer);
}

int SerialPort::read(void) {
  if(rx_head == rx_tail) return -1;
  uint8_t c = rx_buffer[rx_tail];
  rx_tail = (rx_tail + 1) % sizeof(rx_buffer);
  return c;
}

size_t SerialPort::write(uint8_t b) {
  while(!(UCSR0A & _BV(UDRE0)));
  UDR0 = b;
  return 1;
}

size_t SerialPort::print(const char *s) {
  size_t n = 0;
  while(*s) n += write(*s++);
  return n;
}

size_t SerialPort::println(const char *s) {
  size_t n = print(s);
  return n + print("\r\n");
}

void SerialPort::flush(void) {}

/* Startup ----------------------------------------------------------------- */

int main(void) {
  TCCR0A = _BV(WGM01) | _BV(WGM00);
  TCCR0B = _BV(CS01) | _BV(CS00);
  TIMSK0 = _BV(TOIE0);
  sei();
  setup();
  for(;;) loop();
}
