#!/bin/bash
# Noise on the analog bus model (swbb_sim -N / -H, see swbb_sim.c):
# 100 packets of 20 bytes, one attempt each, A -> B, for every implementation
# pair, noise configuration and pull-down. Prints acknowledged / received
# and the level changes the receiver saw on the undriven bus.
# Usage: ./noise_eval.sh "<modes>" "<pairs>" "<noise>" "<pull-downs>"
#   pairs: e.g. "O:O P:P F:F F:W W:W" (O original, P portable, F SWBB_PIN,
#          W SWBB_INTERRUPT_WAKE), noise: "-" (none), "N<rate>:<nC>" or
#          "H<uA>" (several separated by spaces), pull-downs in ohm
#   LIB=<src dir> builds P, F and W from another library version
cd "$(dirname "$0")"
CAP=${CAP:-1500}
flags() {
  local lib=""
  [ -n "${LIB:-}" ] && lib="-I$LIB"
  case $1 in
    O) echo "-Ibuild/orig_src";; P) echo "$lib";; F) echo "$lib -DSWBB_PIN=2";;
    W) echo "$lib -DSWBB_PIN=2 -DSWBB_INTERRUPT_WAKE";;
  esac
}
for m in ${1:-3 7}; do
  for pair in ${2:-F:F F:W}; do
    tx=${pair%%:*}; rx=${pair##*:}
    if [ $m -gt 4 ] && { [ $tx = O ] || [ $tx = P ] || [ $rx = O ] || [ $rx = P ]; }; then
      continue
    fi
    for noise in ${3:--}; do
      case $noise in
        -) nf="";;
        N*) nf="-N ${noise#N}";;
        H*) nf="-H ${noise#H}";;
      esac
      for pull in ${4:-100000}; do
        out=$(SIM_FLAGS="-C $CAP -R 100 -P $pull $nf -s ${SEED:-1}" COUNT=${COUNT:-100} \
          SIM_TIME=${SIM_TIME:-20} ./run_swbb_tests.sh \
          "$(flags $tx) -DSWBB_MODE=$m" "$(flags $rx) -DSWBB_MODE=$m" 2>&1)
        ok=$(echo "$out" | sed -n 's/^A: DONE ok \([0-9]*\) fail.*/\1/p')
        rcv=$(echo "$out" | sed -n 's/^B: DONE received \([0-9]*\).*/\1/p')
        wk=$(echo "$out" | sed -n 's/^B: DONE.*\(wakes [0-9]* stored [0-9]*\).*/\1/p')
        ne=$(echo "$out" | sed -n 's/^sim: noise injections \([0-9]*\).* B \([0-9]*\).*/injections \1, B saw \2 undriven level changes/p')
        printf "mode %s %s->%s noise %-10s pull %6s: acked %3s received %3s %s %s\n" \
          $m $tx $rx "$noise" $pull "${ok:-?}" "${rcv:-?}" "$wk" "$ne"
      done
    done
  done
done
