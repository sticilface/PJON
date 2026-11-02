#!/bin/bash
# RAM and stack of the receiver: B receives COUNT packets from A (SWBB_PIN)
# with LOAD_ISR (other interrupt, default 15us every 256us) on B, and the
# simulator reports B's lowest stack pointer. Prints static RAM (data + bss),
# deepest stack use (RAMEND - lowest SP) and the stack used by the INT0
# interrupt itself (with the interrupts nested in it: it adds to whatever the
# application uses when a frame arrives) for: F (polling), W with 1 and 2
# frames, in modes 3, 6, 7.
# Usage: ./stack_eval.sh [extra receiver flags]  (e.g. -DPJON_PACKET_MAX_LENGTH=80)
cd "$(dirname "$0")"
export COUNT=${COUNT:-100}
export SIM_FLAGS="-S ${SIM_FLAGS:-}"
LOAD=${LOAD:-15}
for m in 3 6 7; do
  for v in "F" "W1" "W2"; do
    case $v in
      F) f="-DSWBB_PIN=2";;
      W1) f="-DSWBB_PIN=2 -DSWBB_INTERRUPT_WAKE -DSWBB_WAKE_FRAMES=1";;
      W2) f="-DSWBB_PIN=2 -DSWBB_INTERRUPT_WAKE -DSWBB_WAKE_FRAMES=2";;
    esac
    out=$(./run_swbb_tests.sh "-DSWBB_PIN=2 -DSWBB_MODE=$m $*" \
      "$f -DSWBB_MODE=$m -DLOAD_ISR=$LOAD $*" 2>&1)
    sp=$(echo "$out" | sed -n 's/^sim: B lowest stack pointer 0x\([0-9A-F]*\).*/\1/p')
    isr=$(echo "$out" | sed -n 's/^sim: B lowest.*INT0 stack \([0-9]*\)/\1/p')
    rx=$(echo "$out" | sed -n 's/^B: DONE received \([0-9]*\) .*/\1/p')
    free=$(echo "$out" | sed -n 's/^B: DONE.* stack_free \([0-9]*\).*/\1/p')
    ram=$(avr-size -A build/B.elf | awk '/^.data/{d=$2}/^.bss/{b=$2}END{print d+b}')
    printf "mode %s %-3s RAM %4d  stack used %3d  INT0 interrupt stack %3d  free (pjon_stack_free) %4s  received %s/%s\n" \
      $m $v $ram $((0x8FF - 0x$sp)) ${isr:-0} "$free" "$rx" $COUNT
  done
done
