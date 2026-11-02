#pragma once

/* CRC32 table-less implementation
   See: http://www.hackersdelight.org/hdcodetxt/crc.c.txt

  Polynomial

  0x82608edb = x^32+x^26+x^23+x^22+x^16+x^12+x^11+x^10+x^8+x^7+x^5+x^4+x^2+x+1
  (0x82608edb; 0x104c11db7) <=> (0xedb88320; 0x1db710641)
                                    |
                                  bit-reversed polynomial implicit +1 notation
                                  or reverse reciprocal notation */

/* By default a 16 entries table (64 bytes, in flash on AVR) is used: about
   3 times faster than the bit by bit computation on a 16MHz ATmega328P,
   same result. Define PJON_CRC32_BITWISE to use the table-less code. */

#if !defined(PJON_CRC32_BITWISE) && defined(__AVR__)
  #include <avr/pgmspace.h>
#endif

struct PJON_crc32 {
#ifndef PJON_CRC32_BITWISE
  static inline uint32_t table(uint8_t i) {
    static const uint32_t t[16]
    #if defined(__AVR__)
      PROGMEM
    #endif
    = {
      0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
      0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
      0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
      0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
    };
    #if defined(__AVR__)
      return pgm_read_dword(&t[i]);
    #else
      return t[i];
    #endif
  };
#endif

  static inline uint32_t compute(
    const uint8_t *data,
    uint16_t length,
    uint32_t previousCrc32 = 0
  ) {
    uint32_t crc = ~previousCrc32; // same as previousCrc32 ^ 0xFFFFFFFF
    uint8_t * current = (uint8_t*) data;
    while(length--) {
      crc ^= *current++;
      #ifdef PJON_CRC32_BITWISE
        uint8_t bits = 8;
        while(bits--) {
          if(crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
          else crc =  crc >> 1;
        }
      #else
        crc = (crc >> 4) ^ table(crc & 15);
        crc = (crc >> 4) ^ table(crc & 15);
      #endif
    }
    return ~crc; // same as crc ^ 0xFFFFFFFF
  };


  static inline bool compare(
    const uint32_t computed,
    const uint8_t *received
  ) {
    for(uint8_t i = 4; i > 0; i--)
      if(
        (uint8_t)(computed >> (8 * (i - 1))) !=
        (uint8_t)(received[3 - (i - 1)])
      ) return false;
    return true;
  };

};
