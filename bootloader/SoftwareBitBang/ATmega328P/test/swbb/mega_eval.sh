#!/bin/bash
# ATmega2560 (bus on Arduino Mega pin 3 = PE5, INT5) with ATmega328P nodes
# (pin 2 = PD2): link tests, one attempt per packet.
# Usage: ./mega_eval.sh <impl> <modes> [ideal|cable]...
#   impl: O (original library, build/orig_src), P (portable), F (SWBB_PIN),
#         W (SWBB_PIN + SWBB_INTERRUPT_WAKE)
# Prints one line per pair: M = Mega, U = 328P (Uno), M->U = Mega transmits.
# Environment: COUNT (100), EXTRA_M / EXTRA_U (extra flags for the Mega /
# the 328P, e.g. -DLOAD_ISR=15), IMPL_U (implementation of the 328P,
# default the same as the Mega), PAIRS (default "M U" "U M" "M M" "U U")
cd "$(dirname "$0")"
flags() { # flags <impl> <pin>
  case $1 in
    O) echo "-Ibuild/orig_src";; P) echo "";;
    F) echo "-DSWBB_PIN=$2";; W) echo "-DSWBB_PIN=$2 -DSWBB_INTERRUPT_WAKE";;
  esac
}
impl=$1; modes=$2; shift 2
impl_u=${IMPL_U:-$impl}
pairs=${PAIRS:-"MU UM MM UU"}
for bus in ${*:-ideal cable}; do
  sim=""; [ $bus = cable ] && sim="-C 1500 -R 100 -P 100000"
  for m in $modes; do
    for pair in $pairs; do
      a=${pair:0:1}; b=${pair:1:1}
      if [ $a = M ]; then ma=atmega2560; pa=3; fa="$(flags $impl 3) ${EXTRA_M:-}"
      else ma=atmega328p; pa=2; fa="$(flags $impl_u 2) ${EXTRA_U:-}"; fi
      if [ $b = M ]; then mb=atmega2560; pb=3; fb="$(flags $impl 3) ${EXTRA_M:-}"
      else mb=atmega328p; pb=2; fb="$(flags $impl_u 2) ${EXTRA_U:-}"; fi
      out=$(MCU_A=$ma MCU_B=$mb PIN_A=$pa PIN_B=$pb \
        SIM_FLAGS="$sim ${SIM_EXTRA:-}" COUNT=${COUNT:-100} \
        ./run_swbb_tests.sh "$fa -DSWBB_MODE=$m" "$fb -DSWBB_MODE=$m" 2>&1)
      ok=$(echo "$out" | sed -n 's/^A: DONE ok \([0-9]*\).*/\1/p')
      us=$(echo "$out" | sed -n 's/^A: DONE.* us \([0-9]*\).*/\1/p')
      rx=$(echo "$out" | sed -n 's/^B: DONE received \([0-9]*\).*/\1/p')
      ct=$(echo "$out" | sed -n 's/.*contention events: \([0-9]*\).*/\1/p')
      err=$(echo "$out" | grep -m1 -o "error: .*" | cut -c1-80)
      printf "%-5s %-3s mode %s %s->%s ack %4s/%s rx %4s %9s us contention %s %s\n" \
        $bus "$impl$([ $impl_u != $impl ] && echo /$impl_u)" $m $a $b \
        "${ok:--}" ${COUNT:-100} "${rx:--}" "${us:--}" "${ct:--}" "$err"
    done
  done
done
