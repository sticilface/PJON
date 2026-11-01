#!/bin/bash
# Flash and RAM of the receiver test sketch (mode 3): F, IA, W, W with 3
# frames in the queue. Built in /tmp, does not touch build/.
cd "$(dirname "$0")"
size() { # size <name> <flags...>
  local n=$1; shift
  avr-g++ "$@" -mmcu=atmega328p -DF_CPU=16000000UL -Os -std=gnu++11 \
    -fno-exceptions -fno-rtti -fno-threadsafe-statics -ffunction-sections \
    -fdata-sections -Wl,--gc-sections -DARDUINO=10819 -I../arduino \
    -I../../../../../src -DSWBB_MODE=${MODE:-3} -DMY_ID=2 -DPEER=1 \
    -x c++ swbb_node.ino -x none ../arduino/arduino_shim.cpp \
    -o /tmp/size_wake_$n.elf || return
  echo "$n $(avr-size -A /tmp/size_wake_$n.elf | awk '/^.text/{t=$2}
    /^.data/{d=$2} /^.bss/{b=$2} END{printf "flash %d RAM %d", t+d, d+b}')"
}
size F -DSWBB_PIN=2
size IA -DSWBB_PIN=2 -DSWBB_INTERRUPT -DSWBB_INTERRUPT_ACK
size W -DSWBB_PIN=2 -DSWBB_INTERRUPT_WAKE
size W3 -DSWBB_PIN=2 -DSWBB_INTERRUPT_WAKE -DSWBB_WAKE_FRAMES=3
size W1 -DSWBB_PIN=2 -DSWBB_INTERRUPT_WAKE -DSWBB_WAKE_FRAMES=1
