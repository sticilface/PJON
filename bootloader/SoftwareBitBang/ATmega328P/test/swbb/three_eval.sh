#!/bin/bash
# three.sh for the implementations: ./three_eval.sh <mode> <tx> <rx>
# F = SWBB_PIN polling, W = SWBB_INTERRUPT_WAKE, O = original library,
# P = portable. Extra flags for all three nodes: EXTRA, for the receiver:
# RX_EXTRA, for the transmitters: TX_EXTRA.
cd "$(dirname "$0")"
flags() {
  case $1 in
    O) echo "-Ibuild/orig_src";; P) echo "";; F) echo "-DSWBB_PIN=2";;
    W) echo "-DSWBB_PIN=2 -DSWBB_INTERRUPT_WAKE";;
  esac
}
out=$(./three.sh "$(flags $2) -DSWBB_MODE=$1 ${EXTRA:-} ${TX_EXTRA:-}" \
  "$(flags $3) -DSWBB_MODE=$1 ${EXTRA:-} ${RX_EXTRA:-}" 2>&1)
a=$(echo "$out" | sed -n 's/^A: DONE ok \([0-9]*\) fail.*/\1/p')
c=$(echo "$out" | sed -n 's/^C: DONE ok \([0-9]*\) fail.*/\1/p')
rx=$(echo "$out" | sed -n 's/^B: DONE received \([0-9]*\) bad \([0-9]*\)\(.*\)$/\1 bad \2 \3/p')
ct=$(echo "$out" | sed -n 's/.*contention events: \([0-9]*\).*/\1/p')
echo "mode $1 $2+$2->$3: acked 1:${a:-?} 3:${c:-?} sum $((${a:-0} + ${c:-0})) | received $rx | contention $ct"
[ -n "${VERBOSE:-}" ] && echo "$out"
exit 0
