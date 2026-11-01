#!/bin/bash
# SoftwareBitBang link tests: two simulated ATmega328P (simavr) on one wire.
# Usage: ./run_swbb_tests.sh <A flags> <B flags>  (A transmits, B receives)
#   e.g. ./run_swbb_tests.sh "-DSWBB_MODE=3" "-DSWBB_MODE=3 -DSWBB_PIN=2"
# Environment: BUS_PIN (default 2), COUNT, LENGTH, SIM_FLAGS (e.g. -h)
set -u
cd "$(dirname "$0")"
HERE=$(pwd)
SRC=$HERE/../../../../../src
SHIM=$HERE/../arduino
BUILD=$HERE/build
mkdir -p "$BUILD"
BUS_PIN=${BUS_PIN:-2}
CXXFLAGS="-mmcu=atmega328p -DF_CPU=16000000UL -Os -std=gnu++11 -fno-exceptions
  -fno-rtti -fno-threadsafe-statics -ffunction-sections -fdata-sections -Wl,--gc-sections
  -DARDUINO=10819 -I$SHIM -I$SRC -DBUS_PIN=$BUS_PIN"
[ -x "$BUILD/swbb_sim" ] && [ "$BUILD/swbb_sim" -nt swbb_sim.c ] || \
  gcc -O2 -Wall -I/usr/include/simavr -o "$BUILD/swbb_sim" swbb_sim.c \
    -lsimavr -lelf -lpthread -lm || exit 2
node() { # node <name> <flags...>
  local name=$1; shift
  avr-g++ "$@" $CXXFLAGS -x c++ swbb_node.ino -x none "$SHIM/arduino_shim.cpp" \
    -o "$BUILD/$name.elf" || exit 2
  avr-objcopy -O ihex -R .eeprom "$BUILD/$name.elf" "$BUILD/$name.hex"
}
node A $1 -DMY_ID=1 -DPEER=2 -DROLE_TX ${COUNT:+-DCOUNT=$COUNT} ${LENGTH:+-DLENGTH=$LENGTH}
node B $2 -DMY_ID=2 -DPEER=1 ${COUNT:+-DCOUNT=$COUNT} ${LENGTH:+-DLENGTH=$LENGTH}
"$BUILD/swbb_sim" -a "$BUILD/A.hex" -b "$BUILD/B.hex" -B $BUS_PIN -t ${SIM_TIME:-20} ${SIM_FLAGS:-}
