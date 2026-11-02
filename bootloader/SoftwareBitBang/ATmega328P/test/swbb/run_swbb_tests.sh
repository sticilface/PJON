#!/bin/bash
# SoftwareBitBang link tests: two simulated ATmega328P (simavr) on one wire.
# Usage: ./run_swbb_tests.sh <A flags> <B flags>  (A transmits, B receives)
#   e.g. ./run_swbb_tests.sh "-DSWBB_MODE=3" "-DSWBB_MODE=3 -DSWBB_PIN=2"
# Environment: BUS_PIN (default 2), COUNT, LENGTH, SIM_FLAGS (e.g. -h),
#   MCU_A / MCU_B (atmega328p default, or atmega2560), PIN_A / PIN_B (bus pin
#   of each node, Arduino numbering of its MCU, default BUS_PIN)
set -u
cd "$(dirname "$0")"
HERE=$(pwd)
SRC=$HERE/../../../../../src
SHIM=$HERE/../arduino
BUILD=$HERE/build
mkdir -p "$BUILD"
BUS_PIN=${BUS_PIN:-2}
MCU_A=${MCU_A:-atmega328p}
MCU_B=${MCU_B:-atmega328p}
PIN_A=${PIN_A:-$BUS_PIN}
PIN_B=${PIN_B:-$BUS_PIN}
CXXFLAGS="-DF_CPU=16000000UL -Os -std=gnu++11 -fno-exceptions
  -fno-rtti -fno-threadsafe-statics -ffunction-sections -fdata-sections -Wl,--gc-sections
  -DARDUINO=10819 -I$SHIM -I$SRC"
[ -x "$BUILD/swbb_sim" ] && [ "$BUILD/swbb_sim" -nt swbb_sim.c ] || \
  gcc -O2 -Wall -I/usr/include/simavr -o "$BUILD/swbb_sim" swbb_sim.c \
    -lsimavr -lelf -lpthread -lm || exit 2
node() { # node <name> <mcu> <pin> <flags...>
  local name=$1 mcu=$2 pin=$3; shift 3
  avr-g++ "$@" -mmcu=$mcu -DBUS_PIN=$pin $CXXFLAGS -x c++ swbb_node.ino -x none "$SHIM/arduino_shim.cpp" \
    -o "$BUILD/$name.elf" || exit 2
  avr-objcopy -O ihex -R .eeprom "$BUILD/$name.elf" "$BUILD/$name.hex"
}
node A $MCU_A $PIN_A $1 -DMY_ID=1 -DPEER=2 -DROLE_TX ${COUNT:+-DCOUNT=$COUNT} ${LENGTH:+-DLENGTH=$LENGTH}
node B $MCU_B $PIN_B $2 -DMY_ID=2 -DPEER=1 ${COUNT:+-DCOUNT=$COUNT} ${LENGTH:+-DLENGTH=$LENGTH}
"$BUILD/swbb_sim" -a "$BUILD/A.hex" -b "$BUILD/B.hex" -B $PIN_A,$PIN_B -M $MCU_A,$MCU_B -t ${SIM_TIME:-20} ${SIM_FLAGS:-}
