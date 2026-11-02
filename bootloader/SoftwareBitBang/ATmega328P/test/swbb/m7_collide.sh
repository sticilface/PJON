#!/bin/bash
# Both devices transmit to each other at the same time (one attempt per
# packet, TX_RECEIVE_US of reception between packets): collisions,
# contention and delivered packets for modes 3, 6 and 7
# Usage: ./m7_collide.sh [receive us between packets]  (CAP as m7_eval.sh)
cd "$(dirname "$0")"
CAP=${CAP:-1500}
SF="-C $CAP -R 100 -P ${PULL:-100000}"
[ "$CAP" = 0 ] && SF=""
for m in "-DSWBB_MODE=3" "-DSWBB_MODE=6" "-DSWBB_MODE=7" \
  "-DSWBB_MODE=7 -DSWBB_INTERRUPT_WAKE"; do
  out=$(COUNT=${COUNT:-200} SIM_TIME=${SIM_TIME:-30} SIM_FLAGS="$SF" \
    ./run_swbb_tests.sh \
    "-DSWBB_PIN=2 $m -DTX_RECEIVE_US=${1:-200}" \
    "-DSWBB_PIN=2 $m -DROLE_TX -DTX_RECEIVE_US=${1:-200}" 2>&1)
  echo "[$m] $(echo "$out" | grep -E 'DONE|contention' | \
    sed 's/ window_ticks.*//; s/sim: simulated/sim/' | tr '\n' ' ')"
done
