#!/bin/bash
# Compiles swbb_node.ino for every MCU / pin / implementation / mode
# combination supported by the direct port access implementations.
cd "$(dirname "$0")"
OUT=$(mktemp -d)
for target in "atmega2560 3" "atmega2560 2" "atmega2560 21" "atmega2560 22" \
  "atmega2560 54" "atmega2560 6" "atmega328p 2" "atmega328p 3" "atmega328p 12"; do
  set -- $target
  mcu=$1; pin=$2
  for v in "" "-DSWBB_INTERRUPT_WAKE" "-DSWBB_INTERRUPT -DSWBB_INTERRUPT_ACK"; do
    for m in 3 6 7; do
      [ "$m" != 3 ] && [ -n "$v" ] && [ "${v#*INTERRUPT_ACK}" != "$v" ] && continue
      r=$(avr-g++ -mmcu=$mcu -DF_CPU=16000000UL -Os -std=gnu++11 \
        -fno-exceptions -fno-rtti -fno-threadsafe-statics -DARDUINO=10819 \
        -I../arduino -I../../../../../src -DSWBB_PIN=$pin -DBUS_PIN=$pin $v \
        -DSWBB_MODE=$m -DMY_ID=2 -DPEER=1 -c -x c++ swbb_node.ino \
        -o $OUT/x.o 2>&1 | grep -m1 -o "error: .*" | cut -c1-110)
      echo "$mcu pin $pin mode $m [${v:-polling}]: ${r:-ok}"
    done
  done
done
rm -rf "$OUT"
