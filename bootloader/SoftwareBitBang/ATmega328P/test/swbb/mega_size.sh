#!/bin/bash
# Flash / RAM of the receiver test sketch (swbb_node.ino) on the Mega (pin 3)
# and on the 328P (pin 2) for each implementation and mode.
cd "$(dirname "$0")"
OUT=$(mktemp -d)
size() { # size <mcu> <pin> <flags...>
  local mcu=$1 pin=$2; shift 2
  avr-g++ -mmcu=$mcu -DF_CPU=16000000UL -Os -std=gnu++11 -fno-exceptions \
    -fno-rtti -fno-threadsafe-statics -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -DARDUINO=10819 -I../arduino "$@" -I../../../../../src \
    -DBUS_PIN=$pin -DMY_ID=2 -DPEER=1 -x c++ swbb_node.ino -x none \
    ../arduino/arduino_shim.cpp -o $OUT/s.elf 2>/dev/null || { echo "-"; return; }
  avr-size -A $OUT/s.elf | awk '/^.text/{t=$2}/^.data/{d=$2}/^.bss/{b=$2}
    END{printf "%d / %d", t+d, d+b}'
}
for target in "atmega2560 3" "atmega328p 2"; do
  set -- $target
  for impl in O P F W; do
    for m in 3 6 7; do
      case $impl in
        O) [ $m != 3 ] && continue; f="-Ibuild/orig_src";;
        P) [ $m != 3 ] && continue; f="";;
        F) f="-DSWBB_PIN=$2";;
        W) f="-DSWBB_PIN=$2 -DSWBB_INTERRUPT_WAKE";;
      esac
      echo "$1 $impl mode $m: $(size $1 $2 $f -DSWBB_MODE=$m)"
    done
  done
done
rm -rf "$OUT"
