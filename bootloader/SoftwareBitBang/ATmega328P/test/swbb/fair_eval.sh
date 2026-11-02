#!/bin/bash
# Fairness and throughput of two saturating transmitters (A id 1, C id 3)
# into one receiver, both retrying back to back for TIME_MS (default 1500),
# for several clock offsets of C. Usage: ./fair_eval.sh "<modes>" "<tx>" "<rx>"
# (implementations as three_eval.sh: F, W, O, P), CLOCKS (C frequencies),
# CAP (analog model, default 1500pF, 0 = ideal wire)
cd "$(dirname "$0")"
CAP=${CAP:-1500}
for m in ${1:-3 6 7}; do
  for tx in ${2:-F}; do
    for rx in ${3:-F W}; do
      for g in ${CLOCKS:-16000000 16008000 16080000 15992000}; do
        cap=""
        [ "$CAP" != 0 ] && cap="-C $CAP"
        line=$(SIM_FLAGS="$cap -G $g" TX_TIME_MS=${TIME_MS:-1500} COUNT=30000 \
          RX_TIMEOUT=$(( ${TIME_MS:-1500} + 200 )) SIM_TIME=$(( ${TIME_MS:-1500} / 1000 + 2 )) \
          ./three_eval.sh $m $tx $rx 2>&1)
        a=$(echo "$line" | sed -n 's/.*acked 1:\([0-9]*\) 3:.*/\1/p')
        c=$(echo "$line" | sed -n 's/.*acked 1:[0-9]* 3:\([0-9]*\) .*/\1/p')
        r=$(echo "$line" | sed -n 's/.*received \([0-9]*\) bad.*/\1/p')
        ct=$(echo "$line" | sed -n 's/.*contention \([0-9]*\).*/\1/p')
        echo "mode $m $tx+$tx->$rx C clock $g: acked $a + $c = $((a + c)), share of A $((100 * a / (a + c + (a + c == 0))))%, received $r, contention $ct"
      done
    done
  done
done
