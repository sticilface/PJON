#!/bin/bash
# Two transmitters (A id 1, C id 3) sending COUNT packets each, back to back,
# to one receiver (B id 2): fairness and acknowledged-but-not-received.
# Usage: ./three.sh "<tx flags>" "<rx flags>"   Environment: COUNT, LENGTH,
# TX_RECEIVE_US (receive time between packets, default 0 = Mode7LinkTest),
# SIM_FLAGS (e.g. "-C 1500"), SIM_TIME
set -u
cd "$(dirname "$0")"
HERE=$(pwd)
SRC=$HERE/../../../../../src
SHIM=$HERE/../arduino
BUILD=$HERE/build
mkdir -p "$BUILD"
COUNT=${COUNT:-500}
CXXFLAGS="-mmcu=atmega328p -DF_CPU=16000000UL -Os -std=gnu++11 -fno-exceptions
  -fno-rtti -fno-threadsafe-statics -ffunction-sections -fdata-sections
  -Wl,--gc-sections -DARDUINO=10819 -I$SHIM -I$SRC -DBUS_PIN=2
  -DCOUNT=$COUNT ${LENGTH:+-DLENGTH=$LENGTH}"
if [ ! -x "$BUILD/swbb_sim" ] || [ swbb_sim.c -nt "$BUILD/swbb_sim" ]; then
  gcc -O2 -Wall -I/usr/include/simavr -o "$BUILD/swbb_sim" swbb_sim.c \
    -lsimavr -lelf -lpthread -lm || exit 2
fi
node() { # node <name> <flags...>
  local name=$1; shift
  avr-g++ "$@" $CXXFLAGS -x c++ swbb_node.ino -x none "$SHIM/arduino_shim.cpp" \
    -o "$BUILD/$name.elf" || exit 2
  avr-objcopy -O ihex -R .eeprom "$BUILD/$name.elf" "$BUILD/$name.hex"
}
node T1 $1 -DMY_ID=1 -DPEER=2 -DROLE_TX -DTX_RECEIVE_US=${TX_RECEIVE_US:-0}
node T3 $1 ${TX2_EXTRA:-} -DMY_ID=${TX2_ID:-3} -DPEER=2 -DROLE_TX -DTX_RECEIVE_US=${TX_RECEIVE_US:-0}
node R2 $2 -DMY_ID=2 -DPEER=1 -DRX_COUNT=$((2 * COUNT)) -DRX_TIMEOUT=${RX_TIMEOUT:-5000}
"$BUILD/swbb_sim" -a "$BUILD/T1.hex" -b "$BUILD/R2.hex" -c "$BUILD/T3.hex" \
  -B 2 -t ${SIM_TIME:-8} ${SIM_FLAGS:-}
