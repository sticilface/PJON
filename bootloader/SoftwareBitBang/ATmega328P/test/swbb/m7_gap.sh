#!/bin/bash
# Mode 7: interrupt window length (SWBB_M7_GAP) vs interrupt load
# Usage: ./m7_gap.sh "<gaps us>" "<loads us>" [window bytes]
cd "$(dirname "$0")"
export COUNT=${COUNT:-100}
GAPS=${1:-24 32 40}
LOADS=${2:-15 20 25}
WINDOW=${3:-4}
for g in $GAPS; do
  for load in $LOADS; do
    line=""
    for p in "F F" "W F" "F W"; do
      set -- $p
      r=$(M7="-DSWBB_M7_GAP=$g -DSWBB_M7_WINDOW=$WINDOW ${M7X:-}" \
        ./m7_eval.sh $1 $2 "-DLOAD_ISR=$load" | \
        sed -n 's/.*ack \([0-9]*\)\/.*/\1/p')
      line="$line $1->$2:$r"
    done
    echo "gap ${g}us window $WINDOW bytes, load ${load}us:$line"
  done
done
