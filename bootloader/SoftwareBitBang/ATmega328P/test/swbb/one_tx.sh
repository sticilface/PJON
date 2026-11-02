#!/bin/bash
# Single transmitter throughput: ./one_tx.sh "<modes>" (F and W receivers,
# COUNT packets, SIM_FLAGS e.g. "-C 1500", LIB=<src dir> for another
# library version)
cd "$(dirname "$0")"
lib=""
[ -n "${LIB:-}" ] && lib="-I$LIB"
for m in ${1:-3 6 7}; do
  for rx in F W; do
    r=""
    [ $rx = W ] && r="-DSWBB_INTERRUPT_WAKE"
    out=$(COUNT=${COUNT:-200} ./run_swbb_tests.sh "$lib -DSWBB_PIN=2 -DSWBB_MODE=$m" \
      "$lib -DSWBB_PIN=2 -DSWBB_MODE=$m $r" 2>&1)
    echo "1 transmitter mode $m F->$rx: $(echo "$out" | grep '^A: DONE' | grep -o 'ok [0-9]*\|us [0-9]*' | tr '\n' ' ')"
  done
done
