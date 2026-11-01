#!/bin/bash
# Link test between a PJON sketch and the bootloader (no application, so it
# stays in the bootloader): the sketch sends COUNT (200) write chunks, one
# attempt each, and prints how many were acknowledged.
# Usage: ./run_boot_link.sh <mode> "<sketch flags>" [bootloader F_CPU]
#   e.g. ./run_boot_link.sh 3 "-DSWBB_PIN=2"
# Environment: BUS_PIN (default 2), COUNT, BL_SRC (bootloader source,
# default ../../pjon_swbb_bootloader.c), BL_FLAGS (extra bootloader flags),
# SIM_FLAGS, SIM_TIME
set -u
cd "$(dirname "$0")"
HERE=$(pwd)
SRC=$HERE/../../../../../src
SHIM=$HERE/../arduino
BUILD=$HERE/build
mkdir -p "$BUILD"
MODE=$1
FLAGS=$2
FCPU=${3:-16000000}
BUS_PIN=${BUS_PIN:-2}
BL_SRC=${BL_SRC:-$HERE/../../pjon_swbb_bootloader.c}
if [ ! -x "$BUILD/swbb_sim" ] || [ swbb_sim.c -nt "$BUILD/swbb_sim" ]; then
  gcc -O2 -Wall -I/usr/include/simavr -o "$BUILD/swbb_sim" swbb_sim.c \
    -lsimavr -lelf -lpthread || exit 2
fi
avr-gcc -mmcu=atmega328p -DF_CPU=${FCPU}UL -Os -std=gnu99 \
  -fno-split-wide-types -mrelax -I"$SRC" -DPJON_PIN=$BUS_PIN -DDEVICE_ID=44 \
  -DMODE=$MODE -DBOOT_START=0x7800 ${BL_FLAGS:-} -nostartfiles \
  -Wl,--section-start=.text=0x7800 -Wl,--relax -o "$BUILD/bl.elf" \
  "$BL_SRC" || exit 2
avr-objcopy -j .text -j .data -O ihex "$BUILD/bl.elf" "$BUILD/bl.hex"
avr-g++ $FLAGS -mmcu=atmega328p -DF_CPU=16000000UL -Os -std=gnu++11 \
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -ffunction-sections \
  -fdata-sections -Wl,--gc-sections -DARDUINO=10819 -I"$SHIM" -I"$SRC" \
  -DBUS_PIN=$BUS_PIN -DSWBB_MODE=$MODE ${COUNT:+-DCOUNT=$COUNT} \
  -x c++ boot_link.ino -x none "$SHIM/arduino_shim.cpp" -o "$BUILD/bu.elf" \
  || exit 2
avr-objcopy -O ihex -R .eeprom "$BUILD/bu.elf" "$BUILD/bu.hex"
"$BUILD/swbb_sim" -a "$BUILD/bu.hex" -b "$BUILD/bl.hex" -r 0x7800 \
  -F $FCPU -B $BUS_PIN -t ${SIM_TIME:-20} ${SIM_FLAGS:-}
