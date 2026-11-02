/* PJON stack check (AVR): how much RAM was never used by the stack
   ___________________________________________________________________________

   The interrupt driven SoftwareBitBang receivers (SWBB_INTERRUPT_WAKE)
   receive a whole frame inside an interrupt, which can happen while the
   application is at its deepest function call: the interrupt's stack
   (~40-50 bytes, nested interrupts included) adds to the application's.
   Use this to check the headroom of the real application:

     #include <utils/PJON_StackCheck.h>
     void setup() {
       pjon_stack_paint();  // first thing in setup()
       ...
     }
     // later, after the heaviest traffic and application work:
     Serial.println(pjon_stack_free());  // bytes the stack never reached

   pjon_stack_paint() fills the free RAM between the heap (or the end of the
   static variables) and the current stack, leaving 16 bytes below it, with
   a pattern; pjon_stack_free() counts how much of the pattern is intact.
   Keep a margin (e.g. 50 bytes or more): the deepest case may not have
   happened during the test. With malloc the heap must not grow after
   pjon_stack_paint() (it would overwrite the pattern and look used).
   ___________________________________________________________________________

   Licensed under the Apache License, Version 2.0 */

#pragma once

#if defined(__AVR__)

#include <stdint.h>
#include <avr/io.h>

extern uint8_t __heap_start;
extern void *__brkval;

#define PJON_STACK_PATTERN 0xA5

static inline uint8_t *pjon_stack_bottom() {
  return __brkval ? (uint8_t *)__brkval : &__heap_start;
}

static void pjon_stack_paint() {
  uint8_t *p = pjon_stack_bottom();
  uint8_t *top = (uint8_t *)SP - 16;
  while(p < top) *p++ = PJON_STACK_PATTERN;
}

static uint16_t pjon_stack_free() {
  const uint8_t *p = pjon_stack_bottom();
  uint16_t n = 0;
  while((p < (const uint8_t *)SP) && (*p == PJON_STACK_PATTERN)) { p++; n++; }
  return n;
}

#endif
