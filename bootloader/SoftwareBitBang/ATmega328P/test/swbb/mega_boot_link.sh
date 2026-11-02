#!/bin/bash
# Link test between an ATmega328P PJON sketch (pin 2) and the ATmega2560
# bootloader (Mega pin 3, BOOT_SIZE=4096, no application so it stays in the
# bootloader): the sketch sends 200 write chunks, one attempt each, and
# prints how many were acknowledged, plus the bus contention count.
# Usage: ./mega_boot_link.sh <mode> "<sketch flags>" [simulator flags]
#   e.g. ./mega_boot_link.sh 7 "-DSWBB_PIN=2" "-C 1500 -R 100 -P 100000"
# Environment: FCPU (bootloader clock, default 16000000), BLF (extra
# bootloader flags)
# Results in REVIEW-swbb-bootloader-2560.md were made with the cable model
# "-C 1500 -R 100 -P 100000", modes 1 and 3 with and without -DSWBB_PIN=2,
# modes 6 and 7 with -DSWBB_PIN=2, and FCPU=8000000 for modes 3, 6, 7.
set -u
cd "$(dirname "$0")"
SW=$(pwd)
T=$SW/..
SRC=$T/../../../../src
SHIM=$T/arduino
OUT=$SW/build/mega_link
mkdir -p "$OUT"
MODE=$1
FLAGS=$2
SIMF=${3:-}
FCPU=${FCPU:-16000000}
avr-gcc -mmcu=atmega2560 -DF_CPU=${FCPU}UL -Os -std=gnu99 \
  -fno-split-wide-types -mrelax -I"$SRC" -DPJON_PIN=3 -DDEVICE_ID=44 \
  -DMODE=$MODE -DBOOT_START=0x3F000 -DLOW_SECTION=0 -fno-jump-tables \
  ${BLF:-} -nostartfiles -Wl,--section-start=.text=0x3F000 -Wl,--relax \
  -o "$OUT/bl.elf" "$T/../pjon_swbb_bootloader.c" || exit 2
avr-objcopy -j .text -j .data -O ihex "$OUT/bl.elf" "$OUT/bl.hex"
avr-g++ $FLAGS -mmcu=atmega328p -DF_CPU=16000000UL -Os -std=gnu++11 \
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -ffunction-sections \
  -fdata-sections -Wl,--gc-sections -DARDUINO=10819 -I"$SHIM" -I"$SRC" \
  -DPJON_BOOT_PAGE_SIZE=256 -DBUS_PIN=2 -DSWBB_MODE=$MODE \
  -x c++ "$SW/boot_link.ino" -x none "$SHIM/arduino_shim.cpp" -o "$OUT/bu.elf" \
  || exit 2
avr-objcopy -O ihex -R .eeprom "$OUT/bu.elf" "$OUT/bu.hex"
[ -x "$SW/build/swbb_sim" ] || gcc -O2 -Wall -I/usr/include/simavr \
  -o "$SW/build/swbb_sim" "$SW/swbb_sim.c" -lsimavr -lelf -lpthread -lm
"$SW/build/swbb_sim" -a "$OUT/bu.hex" -b "$OUT/bl.hex" -r 0x3F000 \
  -M atmega328p,atmega2560 -B 2,3 -F $FCPU -t 30 $SIMF \
  | grep "A: DONE\|contention"
