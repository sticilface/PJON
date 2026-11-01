/* Minimal Arduino core replacement used only by the bootloader simulation
   test, so that the PJON library and the example sketches can be compiled
   with plain avr-g++ without installing the Arduino IDE.

   The functions used by PJON SoftwareBitBang (digitalRead, digitalWrite,
   pinMode, micros, delayMicroseconds) follow the structure of the Arduino AVR
   core so that their execution time, and so the bit timing produced by PJON,
   is close to the one obtained on a real Arduino. */

#pragma once

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HIGH 0x1
#define LOW  0x0
#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2

#define A0 14
#define A1 15
#define A2 16
#define A3 17
#define A4 18
#define A5 19
#define LED_BUILTIN 13

typedef uint8_t byte;
typedef bool boolean;

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);
int analogRead(uint8_t pin);
unsigned long micros(void);
unsigned long millis(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);

class SerialPort {
  public:
    void begin(unsigned long baud);
    int available(void);
    int read(void);
    size_t write(uint8_t b);
    size_t print(const char *s);
    size_t println(const char *s);
    void flush(void);
};

extern SerialPort Serial;

void setup(void);
void loop(void);

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) \
  ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
