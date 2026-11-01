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

#define NOT_ON_TIMER 0
#define PORT_B 2
#define PORT_C 3
#define PORT_D 4

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

/* PWM is never used here, but the lookup is done to keep the timing */
static const uint8_t PROGMEM pin_to_timer[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint16_t PROGMEM port_to_output[] = {
  0, 0, (uint16_t)&PORTB, (uint16_t)&PORTC, (uint16_t)&PORTD
};
static const uint16_t PROGMEM port_to_mode[] = {
  0, 0, (uint16_t)&DDRB, (uint16_t)&DDRC, (uint16_t)&DDRD
};
static const uint16_t PROGMEM port_to_input[] = {
  0, 0, (uint16_t)&PINB, (uint16_t)&PINC, (uint16_t)&PIND
};

static void turn_off_pwm(uint8_t timer) { (void)timer; }

void pinMode(uint8_t pin, uint8_t mode) {
  uint8_t bit = pgm_read_byte(pin_to_mask + pin);
  uint8_t port = pgm_read_byte(pin_to_port + pin);
  volatile uint8_t *reg, *out;
  if(pin > 19) return;
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
  if(pin > 19) return;
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
  if(pin > 19) return LOW;
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

ISR(USART_RX_vect) {
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
