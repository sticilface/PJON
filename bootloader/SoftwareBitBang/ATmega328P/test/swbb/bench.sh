#!/bin/bash
# SoftwareBitBang benchmarks (simulated ATmega328P at 16MHz, pin 2):
# - idle: bus.receive() calls per second with an idle bus, can_start() time
# - can_start: how often can_start() wrongly reports the bus free while
#   another device transmits keep busy bits (waiting for a response)
# Usage: ./bench.sh [modes]   (O, P, F as in matrix.sh)
cd "$(dirname "$0")"
mkdir -p build
if [ ! -d build/orig_src ]; then
  mkdir -p build/orig_src
  git -C "$(git rev-parse --show-toplevel)" archive "${ORIG_REF:-master}" src | tar -x -C build/orig_src \
    --strip-components=1 || exit 2
fi
[ -x build/swbb_sim ] || gcc -O2 -Wall -I/usr/include/simavr \
  -o build/swbb_sim swbb_sim.c -lsimavr -lelf -lpthread || exit 2
SHIM=../arduino
SRC=../../../../../src
CXX="avr-g++ -mmcu=atmega328p -DF_CPU=16000000UL -Os -std=gnu++11
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -ffunction-sections
  -fdata-sections -Wl,--gc-sections -DARDUINO=10819 -I$SHIM"
flags() {
  case $1 in O) echo "-Ibuild/orig_src";; P) echo "";; F) echo "-DSWBB_PIN=2";; esac
}
build() { # build <ino> <out> <flags...>
  local ino=$1 out=$2; shift 2
  $CXX "$@" -I$SRC -x c++ $ino -x none $SHIM/arduino_shim.cpp \
    -o build/$out.elf && avr-objcopy -O ihex build/$out.elf build/$out.hex
}
for m in ${*:-1 3}; do
  for v in O P F; do
    build bench_idle.ino idle $(flags $v) -DSWBB_MODE=$m
    echo "mode $m $v idle: $(./build/swbb_sim -a build/idle.hex \
      -b build/idle.hex -t 3 | sed -n 's/^A: DONE //p')"
  done
  for p in "9.5 52" "7 47"; do
    set -- $p
    build bench_can_start.ino pulser -DSWBB_MODE=$m -DPULSER \
      -DPULSE_US=$1 -DPERIOD_US=$2
    for v in O P F; do
      build bench_can_start.ino cs $(flags $v) -DSWBB_MODE=$m
      echo "mode $m $v keep busy bits ${1}us every ${2}us: $(./build/swbb_sim \
        -a build/cs.hex -b build/pulser.hex -t 3 | sed -n 's/^A: DONE //p')"
    done
  done
done
