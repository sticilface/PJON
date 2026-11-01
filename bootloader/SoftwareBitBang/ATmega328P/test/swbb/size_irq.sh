#!/bin/bash
# Flash / RAM of the link test sketches and idle polling cost: F vs I vs IA
cd "$(dirname "$0")"
SHIM=../arduino
SRC=../../../../../src
CXX="avr-g++ -mmcu=atmega328p -DF_CPU=16000000UL -Os -std=gnu++11
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -ffunction-sections
  -fdata-sections -Wl,--gc-sections -DARDUINO=10819 -I$SHIM -I$SRC"
size() {
  avr-size -A build/sz.elf | awk '/^.text/{t=$2}/^.data/{d=$2}/^.bss/{b=$2}
    END{printf "flash %5d RAM %4d", t+d, d+b}'
}
for v in "F|-DSWBB_PIN=2" "I|-DSWBB_PIN=2 -DSWBB_INTERRUPT" \
  "IA|-DSWBB_PIN=2 -DSWBB_INTERRUPT -DSWBB_INTERRUPT_ACK"; do
  n=${v%%|*}; f=${v#*|}
  $CXX $f -DSWBB_MODE=3 -DMY_ID=1 -DPEER=2 -DROLE_TX -x c++ swbb_node.ino \
    -x none $SHIM/arduino_shim.cpp -o build/sz.elf
  tx=$(size)
  $CXX $f -DSWBB_MODE=3 -DMY_ID=2 -DPEER=1 -x c++ swbb_node.ino \
    -x none $SHIM/arduino_shim.cpp -o build/sz.elf
  rx=$(size)
  $CXX $f -DSWBB_MODE=3 -x c++ bench_idle.ino -x none $SHIM/arduino_shim.cpp \
    -o build/idle.elf && avr-objcopy -O ihex build/idle.elf build/idle.hex
  idle=$(./build/swbb_sim -a build/idle.hex -b build/idle.hex -t 3 |
    sed -n 's/^A: DONE //p')
  echo "$n: tx sketch $tx | rx sketch $rx | $idle"
done
