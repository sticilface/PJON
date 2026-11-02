#!/bin/bash
# Throughput, back to back packets with acknowledgement (one attempt each):
# modes 3 and 6 (SWBB_PIN) and mode 7 (F polling, W wake receiver), payloads
# of 5, 20 and 50 bytes (PJON_PACKET_MAX_LENGTH 80), on the analog bus model
# Usage: ./m7_speed.sh   (CAP as m7_eval.sh)
cd "$(dirname "$0")"
for len in 5 20 50; do
  for p in "3 3" "6 6" "F F" "F W"; do
    set -- $p
    out=$(COUNT=200 LENGTH=$len ./m7_eval.sh $1 $2 \
      "-DTX_RECEIVE_US=0 -DPJON_PACKET_MAX_LENGTH=80")
    ok=$(echo "$out" | sed -n 's/.*ack \([0-9]*\)\/.*/\1/p')
    us=$(echo "$out" | sed -n 's/.* us \([0-9]*\) .*/\1/p')
    awk -v l=$len -v a="$1->$2" -v ok="$ok" -v us="$us" 'BEGIN {
      printf "payload %2d  %-5s ack %s/200  %7.0f us/packet  %6.0f packets/s  %5.2f kB/s\n",
        l, a, ok, us / 200, 200e6 / us, ok * l / us * 1e6 / 1000 }'
  done
done
