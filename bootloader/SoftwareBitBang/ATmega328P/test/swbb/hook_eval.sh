#!/bin/bash
# Frame hooks SWBB_FRAME_BEGIN / SWBB_FRAME_END (swbb_node.ino -DHOOK_TEST):
# begin/end counts, unpaired or nested calls ("bad"), hooks still open at the
# end, on transmitter and receiver, polling (F) and wake (W), with a foreign
# frame (to an absent id) before each packet so that PJON stops reading
# frames early. Usage: ./hook_eval.sh "<modes>"
cd "$(dirname "$0")"
for m in ${1:-3 6 7}; do
  for tx in F W; do
    for rx in F W; do
      t="-DSWBB_PIN=2 -DSWBB_MODE=$m -DHOOK_TEST"
      a="$t"; b="$t"
      [ $tx = W ] && a="$a -DSWBB_INTERRUPT_WAKE"
      [ $rx = W ] && b="$b -DSWBB_INTERRUPT_WAKE"
      out=$(SIM_FLAGS="-C 1500" COUNT=${COUNT:-100} ./run_swbb_tests.sh \
        "$a -DFOREIGN" "$b" 2>&1)
      ta=$(echo "$out" | grep "^A: DONE" | grep -o 'ok [0-9]*\|hooks.*')
      rb=$(echo "$out" | grep "^B: DONE" | grep -o 'received [0-9]*\|hooks.*')
      echo "mode $m $tx->$rx | tx:" $ta "| rx:" $rb
    done
  done
done
