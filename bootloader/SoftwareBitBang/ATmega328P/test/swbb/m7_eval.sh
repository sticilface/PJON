#!/bin/bash
# Mode 7 link test (see REVIEW-swbb-mode7.md)
# Usage: ./m7_eval.sh <tx> <rx> [extra flags for both nodes]
#   tx/rx: F (SWBB_PIN polling) or W (SWBB_INTERRUPT_WAKE) in mode 7,
#          6 or 3 (SWBB_PIN in mode 6 or 3, for comparison)
# Environment: CAP (pF, 0 = ideal bus, default 1500), PULL (default 100000),
#   BFREQ (clock of the receiver B in Hz, default 16000000, +-5% =
#   16800000 / 15200000), COUNT, LENGTH, SIM_TIME, M7 (flags for both nodes,
#   e.g. -DSWBB_M7_BIT=3)
cd "$(dirname "$0")"
CAP=${CAP:-1500}
f() {
  case $1 in
    F) echo "-DSWBB_PIN=2 -DSWBB_MODE=7";;
    W) echo "-DSWBB_PIN=2 -DSWBB_MODE=7 -DSWBB_INTERRUPT_WAKE";;
    6) echo "-DSWBB_PIN=2 -DSWBB_MODE=6";;
    3) echo "-DSWBB_PIN=2 -DSWBB_MODE=3";;
  esac
}
SF=""
[ "$CAP" != 0 ] && SF="-C $CAP -R 100 -P ${PULL:-100000}"
[ -n "${BFREQ:-}" ] && SF="$SF -F $BFREQ"
out=$(SIM_FLAGS="$SF ${SIM_FLAGS:-}" ./run_swbb_tests.sh \
  "$(f $1) ${M7:-} ${3:-}" "$(f $2) ${M7:-} ${3:-}" 2>&1)
echo "$out" | grep -m3 "error"
printf "%s->%s cap %s bfreq %s [%s %s]: ack %s/%s rx %s us %s contention %s\n" \
  "$1" "$2" "$CAP" "${BFREQ:-16000000}" "${M7:-}" "${3:-}" \
  "$(echo "$out" | sed -n 's/^A: DONE ok \([0-9]*\).*/\1/p')" "${COUNT:-200}" \
  "$(echo "$out" | sed -n 's/^B: DONE received \([0-9]*\).*/\1/p')" \
  "$(echo "$out" | sed -n 's/^A: DONE.* us \([0-9]*\).*/\1/p')" \
  "$(echo "$out" | sed -n 's/.*contention events: \([0-9]*\).*/\1/p')"
