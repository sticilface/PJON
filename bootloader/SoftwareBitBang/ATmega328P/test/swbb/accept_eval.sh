#!/bin/bash
# Acknowledgements sent by the interrupt (SWBB_INTERRUPT_WAKE) must match
# what PJON::receive() accepts (SoftwareBitBangAccept.h). A sends packets with
# a header feature, B is a wake receiver with or without the matching
# PJON_INCLUDE_* option. "acked" must never exceed "received".
# Usage: ./accept_eval.sh [modes]   LIB=build/before_src compares with an
# older library (git archive <commit> src into that directory)
cd "$(dirname "$0")"
export COUNT=${COUNT:-50}
I=${LIB:+-I$LIB}
W="-DSWBB_PIN=2 -DSWBB_INTERRUPT_WAKE"
run() { # run <mode> <name> <tx flags> <rx flags>
  local out a b
  out=$(./run_swbb_tests.sh "$I -DSWBB_PIN=2 -DSWBB_MODE=$1 $3" \
    "$I $W -DSWBB_MODE=$1 $4" 2>&1)
  a=$(echo "$out" | sed -n 's/^A: DONE ok \([0-9]*\) fail.*/\1/p')
  b=$(echo "$out" | sed -n 's/^B: DONE received \([0-9]*\) .*/\1/p')
  printf "mode %s %-34s acked %3s received %3s %s\n" $1 "$2" "${a:-?}" "${b:-?}" \
    "$([ "${a:-0}" -gt "${b:-0}" ] && echo '<- ACKED BUT NOT DELIVERED')"
}
for m in ${*:-3 7}; do
  run $m "packet id, rx without" "-DPJON_INCLUDE_PACKET_ID -DTX_PACKET_ID" ""
  run $m "packet id, rx with" "-DPJON_INCLUDE_PACKET_ID -DTX_PACKET_ID" \
    "-DPJON_INCLUDE_PACKET_ID"
  run $m "port, rx without" "-DPJON_INCLUDE_PORT -DTX_PORT=1234" ""
  run $m "port, rx with" "-DPJON_INCLUDE_PORT -DTX_PORT=1234" \
    "-DPJON_INCLUDE_PORT"
  run $m "MAC, rx without" "-DTX_MAC" ""
  run $m "plain" "" ""
done
