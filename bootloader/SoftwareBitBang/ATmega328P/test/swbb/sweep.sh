#!/bin/bash
# Receive window of a receiver: the transmitter (SWBB_PIN implementation)
# sends data bits of SWBB_TX_BIT = PJDL bit + offset, prints the packets
# acknowledged at the first attempt (out of COUNT, default 50) per offset.
# Usage: ./sweep.sh <receiver> <mode> [offsets...]
#   receiver: bl (bootloader at 16MHz), bl8 (bootloader at 8MHz),
#             F (SWBB_PIN), O (original library), P (portable)
# Environment: TX_FLAGS (extra transmitter flags), RX_FLAGS (extra receiver
# flags, for bl: bootloader flags), BL_SRC (bootloader source)
cd "$(dirname "$0")"
RX=$1
MODE=$2
shift 2
OFFSETS=${*:--2 -1.5 -1 -0.5 0 0.5 1 1.5 2 2.5}
export COUNT=${COUNT:-50}
case $MODE in
  1) BIT=44;; 2) BIT=40;; 3) BIT=28;; 4) BIT=26;;
  5) BIT=${SWEEP_BIT5:-16};; 6) BIT=${SWEEP_BIT6:-12};;
esac
line="rx $RX mode $MODE (bit ${BIT}us):"
for d in $OFFSETS; do
  b=$(awk -v a="$BIT" -v d="$d" 'BEGIN { print a + d }')
  TX="-DSWBB_PIN=2 -DSWBB_TX_BIT=$b ${TX_FLAGS:-}"
  case $RX in
    bl) r=$(BL_FLAGS="${RX_FLAGS:-}" ./run_boot_link.sh $MODE "$TX" 2>/dev/null);;
    bl8) r=$(BL_FLAGS="${RX_FLAGS:-}" ./run_boot_link.sh $MODE "$TX" 8000000 \
      2>/dev/null);;
    F) r=$(./run_swbb_tests.sh "$TX -DSWBB_MODE=$MODE" \
      "-DSWBB_PIN=2 -DSWBB_MODE=$MODE ${RX_FLAGS:-}" 2>/dev/null);;
    O) r=$(./run_swbb_tests.sh "$TX -DSWBB_MODE=$MODE" \
      "-Ibuild/orig_src -DSWBB_MODE=$MODE ${RX_FLAGS:-}" 2>/dev/null);;
    P) r=$(./run_swbb_tests.sh "$TX -DSWBB_MODE=$MODE" \
      "-DSWBB_MODE=$MODE ${RX_FLAGS:-}" 2>/dev/null);;
  esac
  ok=$(echo "$r" | sed -n 's/^A: DONE ok \([0-9]*\).*/\1/p')
  line="$line $b:${ok:-x}"
done
echo "$line"
