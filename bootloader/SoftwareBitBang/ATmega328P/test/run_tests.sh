#!/bin/bash
# End to end simulation tests of the PJON SoftwareBitBang bootloader.
#
# Two simulated ATmega328P (simavr) share a single wire bus:
# - target:   the bootloader + the example application with the bootloader hook
# - uploader: the Uploader example sketch, whose serial port is a pty used by
#             tools/pjon_boot_upload.py exactly as with a real Arduino
# The PJON library and the sketches are compiled with avr-g++ using a minimal
# Arduino core replacement (test/arduino) so no Arduino IDE is needed.
#
# Requirements (Debian/Ubuntu):
#   apt install gcc-avr avr-libc binutils-avr libsimavr-dev libelf-dev
#   pip install pyserial
#
# Usage: ./run_tests.sh [test name ...]   (default: all tests)

set -u
cd "$(dirname "$0")"
HERE=$(pwd)
BL_DIR=$HERE/..
SRC=$HERE/../../../../src
EXAMPLES=$HERE/../../../../examples/ARDUINO/Local/SoftwareBitBang/Bootloader
BUILD=$HERE/build
UPLOAD="python3 $BL_DIR/tools/pjon_boot_upload.py"
PTY=/tmp/simavr-uart0
ID=44
mkdir -p "$BUILD"

CXXFLAGS="-mmcu=atmega328p -Os -std=gnu++11 -fno-exceptions -fno-rtti
  -ffunction-sections -fdata-sections -Wl,--gc-sections -DARDUINO=10819
  -I$HERE/arduino -I$SRC"

# sketch <ino> <output name> <extra flags...>
sketch() {
  local ino=$1 out=$2
  shift 2
  avr-g++ $CXXFLAGS -DF_CPU=16000000UL "$@" -x c++ "$ino" -x none \
    "$HERE/arduino/arduino_shim.cpp" "$HERE/app_marker.cpp" \
    -o "$BUILD/$out.elf" || exit 1
  avr-objcopy -O ihex -R .eeprom "$BUILD/$out.elf" "$BUILD/$out.hex"
}

# build <name> <bus pin> <mode> <F_CPU>: bootloader, app v1, app v2, uploader
build() {
  local name=$1 pin=$2 mode=$3 fcpu=$4
  make -s -C "$BL_DIR" clean
  make -s -C "$BL_DIR" PJON_PIN=$pin MODE=$mode F_CPU=$fcpu DEVICE_ID=$ID \
    || exit 1
  cp "$BL_DIR/pjon_swbb_bootloader.hex" "$BUILD/$name-bootloader.hex"
  make -s -C "$BL_DIR" clean
  local flags="-DBUS_PIN=$pin -DSWBB_MODE=$mode"
  sketch "$EXAMPLES/AppWithBootloaderHook/AppWithBootloaderHook.ino" \
    "$name-app_v1" $flags -DAPP_MARKER=1
  sketch "$EXAMPLES/AppWithBootloaderHook/AppWithBootloaderHook.ino" \
    "$name-app_v2" $flags -DAPP_MARKER=2
  sketch "$EXAMPLES/Uploader/Uploader.ino" "$name-uploader" $flags
}

SIM_PID=
# start_sim <name> <bus pin> <sim options...>: starts the simulator
start_sim() {
  local name=$1 pin=$2
  shift 2
  rm -f "$PTY"
  "$BUILD/sim" -b "$BUILD/$name-bootloader.hex" -u "$BUILD/$name-uploader.hex" \
    -B "$pin" "$@" > "$BUILD/sim.log" 2>&1 &
  SIM_PID=$!
  for _ in $(seq 100); do [ -e "$PTY" ] && return; sleep 0.1; done
  echo "simulator did not start"; cat "$BUILD/sim.log"; exit 1
}

# wait_sim: waits for the simulator, returns its exit code
wait_sim() {
  local code
  wait "$SIM_PID"; code=$?
  grep -v "^read_ihex\|uart_pty\|^note:\|^sim: loaded" "$BUILD/sim.log" \
    | sed 's/^/    /'
  return $code
}

PASSED=0
FAILED=0
result() {
  if [ "$1" -eq 0 ]; then
    echo "  PASS"; PASSED=$((PASSED + 1))
  else
    echo "  FAIL"; FAILED=$((FAILED + 1))
  fi
}

# upload_test <name> <pin> [hex]: full update through the application hook
# (app v1 running), checks that the target flash matches the HEX file
upload_test() {
  local name=$1 pin=$2 hex=${3:-$BUILD/$1-app_v2.hex}
  start_sim "$name" "$pin" -a "$BUILD/$name-app_v1.hex" -m 2 -t 120 \
    -f "$BUILD/flash.bin"
  $UPLOAD --port "$PTY" --id $ID --reply-timeout 60 "$hex" \
    | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up=${PIPESTATUS[0]}
  wait_sim; local sim=$?
  python3 client.py check "$hex" "$BUILD/flash.bin" | sed 's/^/    /'
  local flash=${PIPESTATUS[0]}
  [ $up -eq 0 ] && [ $sim -eq 0 ] && [ $flash -eq 0 ]
}

test_hook_upload() {
  echo "hook_upload: app v1 -> bootloader via hook -> app v2 (pin 12, mode 1)"
  build m1 12 1 16000000
  upload_test m1 12
  result $?
}

test_timeout() {
  echo "timeout: enter the bootloader, send nothing, app v1 restarts after 5s"
  build m1 12 1 16000000
  start_sim m1 12 -a "$BUILD/m1-app_v1.hex" -m 1 -w 1 -t 15
  python3 client.py enter "$PTY" $ID | sed 's/^/    /'
  wait_sim; local sim=$?
  # the application must restart between 5 and 6 seconds after the reset
  local delay
  delay=$(awk '/target reset/ {r=$2} /GPIOR1 = 0x01/ {a=$2} END {
    gsub("s","",r); gsub("s","",a); print a - r }' "$BUILD/sim.log")
  echo "    application restarted ${delay}s after entering the bootloader"
  awk -v d="$delay" 'BEGIN { exit !(d >= 5.0 && d <= 6.0) }'
  [ $sim -eq 0 ] && [ $? -eq 0 ]
  result $?
}

test_fresh_chip() {
  echo "fresh_chip: no application, bootloader waits forever, upload app v2"
  build m1 12 1 16000000
  start_sim m1 12 -m 2 -t 120
  sleep 8 # more than the timeout, the bootloader must still be there
  $UPLOAD --port "$PTY" --id $ID --no-enter --reply-timeout 60 \
    "$BUILD/m1-app_v2.hex" | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up=${PIPESTATUS[0]}
  wait_sim; local sim=$?
  [ $up -eq 0 ] && [ $sim -eq 0 ]
  result $?
}

test_interrupted() {
  echo "interrupted: upload stopped after 10 pages, bootloader stays, retry works"
  build m1 12 1 16000000
  start_sim m1 12 -a "$BUILD/m1-app_v1.hex" -m 2 -w 1 -t 120
  python3 client.py partial "$PTY" $ID "$BUILD/m1-app_v2.hex" 10 \
    | sed 's/^/    /'
  sleep 8 # more than the timeout: there is no application to start
  $UPLOAD --port "$PTY" --id $ID --no-enter --reply-timeout 60 \
    "$BUILD/m1-app_v2.hex" | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up=${PIPESTATUS[0]}
  wait_sim; local sim=$?
  # the application must have run only twice: v1 at power on and v2 at the end
  local runs
  runs=$(grep -c "running application" "$BUILD/sim.log")
  echo "    application started $runs times (expected 2)"
  [ $up -eq 0 ] && [ $sim -eq 0 ] && [ "$runs" -eq 2 ]
  result $?
}

test_pins() {
  for pin in 2 17; do
    echo "pin_$pin: full update using Arduino pin $pin"
    build p$pin $pin 1 16000000
    upload_test p$pin $pin
    result $?
  done
}

test_modes() {
  for mode in 2 3 4; do
    echo "mode_$mode: full update using SoftwareBitBang mode $mode"
    build m$mode 12 $mode 16000000
    upload_test m$mode 12
    result $?
  done
}

test_8mhz() {
  echo "8mhz: bootloader built for and running at 8MHz, fresh chip upload"
  build f8 12 1 8000000
  start_sim f8 12 -F 8000000 -m 2 -t 120
  $UPLOAD --port "$PTY" --id $ID --no-enter --reply-timeout 60 \
    "$BUILD/f8-app_v2.hex" | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up=${PIPESTATUS[0]}
  wait_sim; local sim=$?
  [ $up -eq 0 ] && [ $sim -eq 0 ]
  result $?
}

test_large_image() {
  echo "large_image: application area filled up to the bootloader (31kB)"
  build m1 12 1 16000000
  python3 client.py large "$BUILD/m1-app_v2.hex" "$BUILD/m1-large.hex" \
    | sed 's/^/    /'
  upload_test m1 12 "$BUILD/m1-large.hex"
  result $?
}

test_protection() {
  echo "protection: bootloader section and unaligned addresses are refused"
  build m1 12 1 16000000
  start_sim m1 12 -t 120
  python3 client.py protect "$PTY" $ID | sed 's/^/    /'
  local client=${PIPESTATUS[0]}
  kill "$SIM_PID" 2> /dev/null
  wait "$SIM_PID" 2> /dev/null
  [ $client -eq 0 ]
  result $?
}

gcc -O2 -Wall -I/usr/include/simavr -o "$BUILD/sim" sim.c \
  -lsimavrparts -lsimavr -lelf -lpthread -lutil || exit 1

TESTS=${*:-hook_upload timeout fresh_chip interrupted pins modes 8mhz \
  large_image protection}
for t in $TESTS; do "test_$t"; done
echo "passed: $PASSED, failed: $FAILED"
[ $FAILED -eq 0 ]
