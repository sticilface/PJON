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
# Environment:
#   DIRECT_IO=1     build the sketches with SWBB_PIN (direct port access)
#   SKETCH_FLAGS    extra compiler flags for the sketches
#   ORIG_SRC        build the sketches with the PJON library in this
#                   directory (e.g. a previous version) instead of ../src
#   BL_SOURCE       build this bootloader source (absolute path) instead of
#                   pjon_swbb_bootloader.c (e.g. a previous version)

set -u
cd "$(dirname "$0")"
HERE=$(pwd)
BL_DIR=$HERE/..
SRC=$HERE/../../../../src
EXAMPLES=$HERE/../../../../examples/ARDUINO/Local/SoftwareBitBang/Bootloader
BUILD=$HERE/build
UPLOAD="python3 $BL_DIR/tools/pjon_boot_upload.py"
PTY=/tmp/simavr-uart0
TARGET_PTY=/tmp/simavr-target0
# Reset vector of the default 3kB layout (2kB hardware boot section)
RESET_ADDRESS=0x7800
AVRDUDE="avrdude -q -q -c arduino -p m328p -b 115200 -P $TARGET_PTY"
ID=44
mkdir -p "$BUILD"

CXXFLAGS="-mmcu=atmega328p -Os -std=gnu++11 -fno-exceptions -fno-rtti
  -ffunction-sections -fdata-sections -Wl,--gc-sections -DARDUINO=10819
  -I$HERE/arduino -I$SRC"

# sketch <ino> <output name> <extra flags...>
sketch() {
  local ino=$1 out=$2
  shift 2
  avr-g++ ${ORIG_SRC:+-I$ORIG_SRC} $CXXFLAGS -DF_CPU=16000000UL "$@" -x c++ "$ino" -x none \
    "$HERE/arduino/arduino_shim.cpp" "$HERE/app_marker.cpp" \
    -o "$BUILD/$out.elf" || exit 1
  avr-objcopy -O ihex -R .eeprom "$BUILD/$out.elf" "$BUILD/$out.hex"
}

# build <name> <bus pin> <mode> <F_CPU> [sketch flags] [make options]:
# bootloader, app v1, app v2, uploader and an app without boot guard
# confirmation. Sketch flags are added to the PJON sketches, make options
# to the bootloader build (default DEVICE_ID=44).
build() {
  local name=$1 pin=$2 mode=$3 fcpu=$4 extra=${5:-} options=${6-DEVICE_ID=$ID}
  make -s -C "$BL_DIR" clean
  make -s -C "$BL_DIR" PJON_PIN=$pin MODE=$mode F_CPU=$fcpu $options \
    ${BL_SOURCE:+SOURCE=$BL_SOURCE} \
    || exit 1
  cp "$BL_DIR/pjon_swbb_bootloader.hex" "$BUILD/$name-bootloader.hex"
  make -s -C "$BL_DIR" clean
  local flags="-DBUS_PIN=$pin -DSWBB_MODE=$mode $extra ${SKETCH_FLAGS:-}"
  [ "${DIRECT_IO:-0}" = 1 ] && flags="$flags -DSWBB_PIN=$pin"
  sketch "$EXAMPLES/AppWithBootloaderHook/AppWithBootloaderHook.ino" \
    "$name-app_v1" $flags -DAPP_MARKER=1
  sketch "$EXAMPLES/AppWithBootloaderHook/AppWithBootloaderHook.ino" \
    "$name-app_v2" $flags -DAPP_MARKER=2
  sketch "$EXAMPLES/Uploader/Uploader.ino" "$name-uploader" $flags
  sketch "$HERE/app_noguard.cpp" "$name-app_noguard" -DAPP_MARKER=3
}

SIM_PID=
# start_sim <name> <bus pin> <sim options...>: starts the simulator
start_sim() {
  local name=$1 pin=$2
  shift 2
  rm -f "$PTY"
  "$BUILD/sim" -b "$BUILD/$name-bootloader.hex" -u "$BUILD/$name-uploader.hex" \
    -R $RESET_ADDRESS -B "$pin" "$@" > "$BUILD/sim.log" 2>&1 &
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
  echo "interrupted: upload stopped in the middle of a page, bootloader stays,"
  echo "  retry works"
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

# provision_flow <name> <pin> <mode> <F_CPU> <sketch flags>: a fresh device
# announces itself, gets id 45 and app v1, then app v1 is updated to app v2
# through the hook. With a bootloader at 8MHz only the first part (the test
# applications are built for 16MHz, they can not use the bus at 8MHz).
provision_flow() {
  local name=$1 pin=$2 mode=$3 fcpu=$4 extra=$5 first=v2
  [ $fcpu = 16000000 ] && first=v1
  build $name $pin $mode $fcpu "$extra" ""
  start_sim $name $pin -F $fcpu -m 2 -t 120 -E "$BUILD/$name-eeprom"
  $UPLOAD --port "$PTY" --provision 45 --reply-timeout 60 \
    "$BUILD/$name-app_$first.hex" | tr '\r' '\n' | grep -v '^writing' \
    | sed 's/^/    /'
  local up=${PIPESTATUS[0]}
  if [ $fcpu = 16000000 ]; then
    $UPLOAD --port "$PTY" --id 45 --reply-timeout 60 "$BUILD/$name-app_v2.hex" \
      | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
    local up2=${PIPESTATUS[0]}
    [ $up2 -eq 0 ] || up=1
  fi
  wait_sim; local sim=$?
  python3 client.py eeprom "$BUILD/$name-eeprom0.bin" 45 | sed 's/^/    /'
  local ee=${PIPESTATUS[0]}
  [ $up -eq 0 ] && [ $sim -eq 0 ] && [ $ee -eq 0 ]
}

test_announce_modes() {
  for mode in 3 6 7; do
    echo "provision_mode_$mode: announcement, id assignment and updates in"
    echo "  mode $mode (pin 2, SWBB_PIN)"
    provision_flow pm$mode 2 $mode 16000000 "-DSWBB_PIN=2"
    result $?
  done
  echo "provision_mode_7_8mhz: announcement, id assignment and upload with"
  echo "  the bootloader at 8MHz (mode 7)"
  provision_flow pm7f8 2 7 8000000 "-DSWBB_PIN=2"
  result $?
  echo "boot_guard_mode_7: application that never confirms its start, the"
  echo "  bootloader announces the failure in mode 7, a working app is flashed"
  build bg7 2 7 16000000 "-DSWBB_PIN=2"
  start_sim bg7 2 -a "$BUILD/bg7-app_v1.hex" -i 44 -s 0A0B0C0D -m 0xFE -t 50 \
    -E "$BUILD/bg7-eeprom"
  $UPLOAD --port "$PTY" --id $ID --reply-timeout 60 "$BUILD/bg7-app_noguard.hex" \
    | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up1=${PIPESTATUS[0]}
  # the watchdog reset comes 8 simulated seconds after the update: wait in
  # real time long enough even if the simulation runs slower than real time
  timeout 90 $UPLOAD --port "$PTY" --listen --wait 40 --reply-timeout 60 \
    > "$BUILD/listen.log"
  sed 's/^/    /' "$BUILD/listen.log"
  $UPLOAD --port "$PTY" --id $ID --no-enter --reply-timeout 60 \
    "$BUILD/bg7-app_v2.hex" | tr '\r' '\n' | grep -v '^writing' \
    | sed 's/^/    /'
  local up2=${PIPESTATUS[0]}
  wait_sim
  local announced
  announced=$(grep -c "id 44 serial 0a0b0c0d: application did not start" \
    "$BUILD/listen.log")
  [ "$announced" -ge 1 ] && [ $up1 -eq 0 ] && [ $up2 -eq 0 ]
  result $?
  echo "serial_mode_7: serial upload (Optiboot protocol) of app v1 with a"
  echo "  mode 7 bootloader, then update to app v2 over PJON in mode 7"
  build sm7 2 7 16000000 "-DSWBB_PIN=2"
  start_sim sm7 2 -S -m 2 -t 120 -f "$BUILD/flash.bin"
  $AVRDUDE -U flash:w:"$BUILD/sm7-app_v1.hex":i 2>&1 \
    | grep -v "TIOCMGET" | sed 's/^/    /'
  local serial=${PIPESTATUS[0]}
  $UPLOAD --port "$PTY" --id $ID --reply-timeout 60 "$BUILD/sm7-app_v2.hex" \
    | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up=${PIPESTATUS[0]}
  wait_sim; local sim=$?
  python3 client.py check "$BUILD/sm7-app_v2.hex" "$BUILD/flash.bin" \
    | sed 's/^/    /'
  local flash=${PIPESTATUS[0]}
  [ $serial -eq 0 ] && [ $up -eq 0 ] && [ $sim -eq 0 ] && [ $flash -eq 0 ]
  result $?
}

test_fast_modes() {
  for mode in 5 6; do
    echo "mode_$mode: full update using the fast mode $mode (SWBB_PIN, pin 2)"
    build fm$mode 2 $mode 16000000 "-DSWBB_PIN=2"
    upload_test fm$mode 2
    result $?
  done
  echo "mode_5_8mhz: fast mode 5, bootloader at 8MHz, fresh chip upload"
  build f8m5 2 5 8000000 "-DSWBB_PIN=2"
  start_sim f8m5 2 -F 8000000 -m 2 -t 120
  $UPLOAD --port "$PTY" --id $ID --no-enter --reply-timeout 60 \
    "$BUILD/f8m5-app_v2.hex" | tr '\r' '\n' | grep -v '^writing' \
    | sed 's/^/    /'
  local up=${PIPESTATUS[0]}
  wait_sim; local sim=$?
  [ $up -eq 0 ] && [ $sim -eq 0 ]
  result $?
}

test_mode7() {
  echo "mode_7: full update using mode 7 (not PJDL, SWBB_PIN, pin 2)"
  build m7 2 7 16000000 "-DSWBB_PIN=2"
  upload_test m7 2
  result $?
  echo "mode_7_wake: full update using mode 7, application and uploader with"
  echo "  SWBB_INTERRUPT_WAKE"
  build m7w 2 7 16000000 "-DSWBB_PIN=2 -DSWBB_INTERRUPT_WAKE"
  upload_test m7w 2
  result $?
  echo "mode_7_8mhz: mode 7, bootloader at 8MHz, fresh chip upload"
  build f8m7 2 7 8000000 "-DSWBB_PIN=2"
  start_sim f8m7 2 -F 8000000 -m 2 -t 120
  $UPLOAD --port "$PTY" --id $ID --no-enter --reply-timeout 60 \
    "$BUILD/f8m7-app_v2.hex" | tr '\r' '\n' | grep -v '^writing' \
    | sed 's/^/    /'
  local up=${PIPESTATUS[0]}
  wait_sim; local sim=$?
  [ $up -eq 0 ] && [ $sim -eq 0 ]
  result $?
}

test_large_image() {
  echo "large_image: application area filled up to the bootloader (29kB)"
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

test_router() {
  echo "router: mode 3, pin 2, sender with PJON_PACKET_MAX_LENGTH 50, packet id,"
  echo "  port, sender info and default SWBB_RESPONSE_OFFSET (Melvan router setup)"
  build r3 2 3 16000000 "-DPJON_INCLUDE_PACKET_ID -DPJON_PACKET_MAX_LENGTH=50"
  upload_test r3 2
  result $?
}

test_chunks() {
  echo "chunks: missing, out of order and repeated chunks"
  build m1 12 1 16000000
  start_sim m1 12 -t 300
  python3 client.py chunks "$PTY" $ID | sed 's/^/    /'
  local client=${PIPESTATUS[0]}
  kill "$SIM_PID" 2> /dev/null
  wait "$SIM_PID" 2> /dev/null
  [ $client -eq 0 ]
  result $?
}

test_provision() {
  echo "provision: fresh device (erased EEPROM, serial generated by the"
  echo "  bootloader) announces itself, gets id 45 and app v1, then app v1"
  echo "  (using id 45 from EEPROM) is updated to app v2 through the hook"
  build pv 12 1 16000000 "" ""
  start_sim pv 12 -m 2 -t 120 -E "$BUILD/pv-eeprom"
  $UPLOAD --port "$PTY" --provision 45 --reply-timeout 60 "$BUILD/pv-app_v1.hex" \
    | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up=${PIPESTATUS[0]}
  $UPLOAD --port "$PTY" --id 45 --reply-timeout 60 "$BUILD/pv-app_v2.hex" \
    | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up2=${PIPESTATUS[0]}
  [ $up2 -eq 0 ] || up=1
  wait_sim; local sim=$?
  python3 client.py eeprom "$BUILD/pv-eeprom0.bin" 45 | sed 's/^/    /'
  local ee=${PIPESTATUS[0]}
  [ $up -eq 0 ] && [ $sim -eq 0 ] && [ $ee -eq 0 ]
  result $?
}

test_provision_two() {
  echo "provision_two: two fresh devices on the bus at once (serials written"
  echo "  at ISP time), a wrong serial is refused, they get ids 45 and 46"
  build pv 12 1 16000000 "" ""
  start_sim pv 12 -N 2 -s 11111111 -s 22222222 -m 2 -t 300 \
    -E "$BUILD/pv2-eeprom"
  python3 client.py assign "$PTY" 33333333 45 0 | sed 's/^/    /'
  local wrong=${PIPESTATUS[0]}
  $UPLOAD --port "$PTY" --provision 45 --reply-timeout 60 "$BUILD/pv-app_v2.hex" \
    | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up1=${PIPESTATUS[0]}
  $UPLOAD --port "$PTY" --provision 46 --reply-timeout 60 "$BUILD/pv-app_v2.hex" \
    | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up2=${PIPESTATUS[0]}
  wait_sim; local sim=$?
  local ids
  ids=$(for n in 0 1; do python3 client.py eeprom "$BUILD/pv2-eeprom$n.bin" 0 \
    | sed 's/.*device id \([0-9]*\).*/\1/'; done | sort | tr '\n' ' ')
  echo "    device ids in EEPROM: $ids"
  [ $wrong -eq 0 ] && [ $up1 -eq 0 ] && [ $up2 -eq 0 ] && [ $sim -eq 0 ] && \
    [ "$ids" = "45 46 " ]
  result $?
}

test_boot_guard() {
  echo "boot_guard: an update with an application that never confirms its"
  echo "  start is reset by the watchdog, the bootloader stays and announces"
  echo "  it, a working app is flashed and keeps running (no more resets)"
  build m1 12 1 16000000
  start_sim m1 12 -a "$BUILD/m1-app_v1.hex" -i 44 -s 0A0B0C0D -m 0xFE -t 50 \
    -E "$BUILD/bg-eeprom"
  $UPLOAD --port "$PTY" --id $ID --reply-timeout 60 "$BUILD/m1-app_noguard.hex" \
    | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up1=${PIPESTATUS[0]}
  timeout 60 $UPLOAD --port "$PTY" --listen --wait 15 --reply-timeout 60 \
    > "$BUILD/listen.log"
  sed 's/^/    /' "$BUILD/listen.log"
  $UPLOAD --port "$PTY" --id $ID --no-enter --reply-timeout 60 \
    "$BUILD/m1-app_v2.hex" | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up2=${PIPESTATUS[0]}
  wait_sim
  # the announcement, then app v2 running for at least 10s without reset
  local announced after
  announced=$(grep -c "id 44 serial 0a0b0c0d: application did not start" \
    "$BUILD/listen.log")
  after=$(awk '/GPIOR1 = 0x02/ {t=$2; resets=0} /target reset/ {resets++}
    /^sim: simulated/ {gsub("s","",t); gsub("s,","",$3); print ($3 - t) " " resets}' \
    "$BUILD/sim.log")
  echo "    app v2 ran ${after% *}s, resets after it: ${after#* }"
  [ "$announced" -ge 1 ] && [ $up1 -eq 0 ] && [ $up2 -eq 0 ] && \
    awk -v a="$after" 'BEGIN { split(a, v, " "); exit !(v[1] >= 10 && v[2] == 0) }'
  result $?
}

test_serial_fresh() {
  echo "serial_fresh: fresh chip, app v1 uploaded over serial with avrdude"
  echo "  (Optiboot protocol, written and verified), then updated to app v2"
  echo "  over PJON through the hook"
  build m1 12 1 16000000
  start_sim m1 12 -S -m 2 -t 120 -f "$BUILD/flash.bin"
  $AVRDUDE -U flash:w:"$BUILD/m1-app_v1.hex":i 2>&1 \
    | grep -v "TIOCMGET" | sed 's/^/    /'
  local serial=${PIPESTATUS[0]}
  echo "    avrdude exit code: $serial"
  $UPLOAD --port "$PTY" --id $ID --reply-timeout 60 "$BUILD/m1-app_v2.hex" \
    | tr '\r' '\n' | grep -v '^writing' | sed 's/^/    /'
  local up=${PIPESTATUS[0]}
  wait_sim; local sim=$?
  python3 client.py check "$BUILD/m1-app_v2.hex" "$BUILD/flash.bin" | sed 's/^/    /'
  local flash=${PIPESTATUS[0]}
  [ $serial -eq 0 ] && [ $up -eq 0 ] && [ $sim -eq 0 ] && [ $flash -eq 0 ]
  result $?
}

test_serial_reset() {
  echo "serial_reset: app v1 running, reset pin (as the Arduino IDE does with"
  echo "  DTR), serial upload of an app that never calls pjon_boot_app_ok():"
  echo "  serial uploads do not arm the boot guard, it keeps running"
  build sr 12 1 16000000 "" "DEVICE_ID=$ID EXTRF_TIMEOUT_MS=4000"
  start_sim sr 12 -a "$BUILD/sr-app_v1.hex" -S -X -m 0xFE -t 25
  $AVRDUDE -D -U flash:w:"$BUILD/sr-app_noguard.hex":i 2>&1 \
    | grep -v "TIOCMGET" | sed 's/^/    /'
  local serial=${PIPESTATUS[0]}
  echo "    avrdude exit code: $serial"
  wait_sim
  local after
  after=$(awk '/GPIOR1 = 0x03/ {t=$2; resets=0} /target reset/ {resets++}
    /^sim: simulated/ {gsub("s","",t); gsub("s,","",$3); print ($3 - t) " " resets}' \
    "$BUILD/sim.log")
  echo "    uploaded app ran ${after% *}s, resets: ${after#* }"
  [ $serial -eq 0 ] && \
    awk -v a="$after" 'BEGIN { split(a, v, " "); exit !(v[1] >= 10 && v[2] == 0) }'
  result $?
}

gcc -O2 -Wall -I/usr/include/simavr -o "$BUILD/sim" sim.c \
  -lsimavrparts -lsimavr -lelf -lpthread -lutil || exit 1

TESTS=${*:-hook_upload timeout fresh_chip interrupted pins modes 8mhz \
  large_image protection router chunks fast_modes mode7 provision \
  provision_two boot_guard serial_fresh serial_reset announce_modes}
for t in $TESTS; do "test_$t"; done
echo "passed: $PASSED, failed: $FAILED"
[ $FAILED -eq 0 ]
