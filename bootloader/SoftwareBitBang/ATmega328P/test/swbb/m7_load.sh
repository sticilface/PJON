#!/bin/bash
# Mode 7 under interrupt load (Timer2 handler every 256us busy LOAD us, on
# both nodes), then at 3nF and on the ideal bus
# Usage: ./m7_load.sh   (COUNT default 100)
cd "$(dirname "$0")"
export COUNT=${COUNT:-100}
for load in 5 10 15 20 25; do
  for p in "F F" "F W" "W F"; do
    set -- $p
    ./m7_eval.sh $1 $2 "-DLOAD_ISR=$load" | sed "s/^/load ${load}us /"
  done
done
for cap in 3000 0; do
  for p in "F F" "F W" "W F" "W W"; do
    set -- $p
    CAP=$cap ./m7_eval.sh $1 $2
  done
done
