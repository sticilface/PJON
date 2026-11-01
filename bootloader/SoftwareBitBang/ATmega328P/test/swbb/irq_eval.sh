#!/bin/bash
# Evaluation of the interrupt driven receiver (SWBB_INTERRUPT) against the
# polling implementations. Usage: ./irq_eval.sh <section> (default: all)
cd "$(dirname "$0")"
O="-Ibuild/orig_src"
P=""
F="-DSWBB_PIN=2"
I="-DSWBB_PIN=2 -DSWBB_INTERRUPT"
IA="-DSWBB_PIN=2 -DSWBB_INTERRUPT -DSWBB_INTERRUPT_ACK"
run() { # run <label> <A flags> <B flags>
  local out
  out=$(./run_swbb_tests.sh "$2" "$3" 2>&1)
  printf "%-34s ack %4s  rx %-14s %9s us  contention %s\n" "$1" \
    "$(echo "$out" | sed -n 's/^A: DONE ok \([0-9]*\) fail.*/\1/p')" \
    "$(echo "$out" | sed -n 's/^B: DONE received \(.*\)/\1/p')" \
    "$(echo "$out" | sed -n 's/^A: DONE.* us \([0-9]*\).*/\1/p')" \
    "$(echo "$out" | sed -n 's/.*contention events: \([0-9]*\).*/\1/p')"
  echo "$out" | grep -i "error" | head -3
}
name() { case "$1" in "$O") echo O;; "$P") echo P;; "$F") echo F;;
  "$I") echo I;; "$IA") echo IA;; esac; }

section=${1:-all}
if [ "$section" = smoke ] || [ "$section" = all ]; then
  COUNT=${COUNT:-100}; export COUNT
  for m in ${MODES:-3}; do
    run "mode $m F->I" "$F -DSWBB_MODE=$m" "$I -DSWBB_MODE=$m"
    run "mode $m F->IA" "$F -DSWBB_MODE=$m" "$IA -DSWBB_MODE=$m"
  done
fi
if [ "$section" = matrix ] || [ "$section" = all ]; then
  for m in ${MODES:-1 2 3 4}; do
    for tx in "$O" "$P" "$F" "$I"; do
      for rx in "$I" "$IA"; do
        run "mode $m $(name "$tx")->$(name "$rx")" "$tx -DSWBB_MODE=$m" \
          "$rx -DSWBB_MODE=$m"
      done
    done
    run "mode $m I->O" "$I -DSWBB_MODE=$m" "$O -DSWBB_MODE=$m"
    run "mode $m I->F" "$I -DSWBB_MODE=$m" "$F -DSWBB_MODE=$m"
  done
fi
if [ "$section" = busy ] || [ "$section" = all ]; then
  # The receiver's loop is busy for BUSY_US between bus.receive() calls
  for m in ${MODES:-3}; do
    for busy in ${BUSY:-0 200 500 1000 3000 10000}; do
      for rx in "$F" "$I" "$IA"; do
        run "mode $m busy $busy F->$(name "$rx")" "$F -DSWBB_MODE=$m" \
          "$rx -DSWBB_MODE=$m -DBUSY_US=$busy -DRX_TIMEOUT=8000"
      done
    done
  done
fi
if [ "$section" = load ] || [ "$section" = all ]; then
  # Interrupt load (15us every 256us) on the receiver
  for m in ${MODES:-1 3}; do
    for rx in "$F" "$I" "$IA"; do
      for isr in ${ISR:-5 10 15}; do
        run "mode $m F->$(name "$rx") rx ISR ${isr}us" "$F -DSWBB_MODE=$m" \
          "$rx -DSWBB_MODE=$m -DLOAD_ISR=$isr"
      done
    done
  done
fi
