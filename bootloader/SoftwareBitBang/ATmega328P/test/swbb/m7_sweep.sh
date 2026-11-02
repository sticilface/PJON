#!/bin/bash
# Mode 7 bit time sweep: clock tolerance of the receiver for each bit time
# Usage: ./m7_sweep.sh "<bit times us>" "<clock errors %>" [tx] [rx]
# (environment as m7_eval.sh, M7X adds flags to both nodes)
cd "$(dirname "$0")"
for t in ${1:-4 3 2.5 2}; do
  line=""
  for p in ${2:--5 -4 -3 0 3 4 5}; do
    r=$(BFREQ=$(( 16000000 + 160000 * p )) M7="-DSWBB_M7_BIT=$t ${M7X:-}" \
      ./m7_eval.sh ${3:-F} ${4:-F} "-DRX_TIMEOUT=2000" | \
      sed -n 's/.*ack \([0-9]*\/[0-9]*\).*/\1/p')
    line="$line ${p}%:$r"
  done
  echo "bit ${t}us:$line"
done
