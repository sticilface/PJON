#!/bin/bash
# Mega (pin 3) with interrupt load and busy loops, 328P (pin 2) partner,
# cable model. Usage: ./mega_load.sh [load|busy]
cd "$(dirname "$0")"
export COUNT=${COUNT:-100}
case ${1:-load} in
  load) # Timer2 interrupt every 256us busy for N us on the Mega only
    for impl in F W; do
      for m in 3 6 7; do
        for isr in 5 10 15 20 25; do
          r=$(PAIRS="UM MU" EXTRA_M="-DLOAD_ISR=$isr" ./mega_eval.sh $impl $m cable \
            | awk '{printf "%s %s  ", $5, $7}')
          echo "$impl mode $m Mega ISR ${isr}us: $r"
        done
      done
    done;;
  busy) # the Mega (receiver) is busy BUSY_US between bus.receive() calls
    for impl in F W; do
      for m in 3 6 7; do
        for b in 0 1000 3000 10000; do
          for gap in 0 10; do
            extra="-DBUSY_US=$b"
            [ $gap != 0 ] && extra_u="-DTX_GAP_MS=$gap" || extra_u=""
            r=$(PAIRS="UM" IMPL_U=F EXTRA_M="$extra" EXTRA_U="$extra_u" \
              SIM_EXTRA="-t 30" ./mega_eval.sh $impl $m cable | awk '{print $7}')
            echo "$impl mode $m Mega busy ${b}us, packet gap ${gap}ms: received $r"
          done
        done
      done
    done;;
esac
