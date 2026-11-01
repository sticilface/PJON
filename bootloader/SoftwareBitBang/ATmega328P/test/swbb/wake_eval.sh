#!/bin/bash
# Evaluation of the interrupt triggered receiver (SWBB_INTERRUPT_WAKE, W)
# against the polling one (F) and the time stamping one (IA).
# Usage: ./wake_eval.sh <section> [modes]
#   pair <A> <B> [mode]  one run, A and B among O P F I IA W
#   matrix   every sender to W, W to every receiver
#   busy     receiver busy between bus.receive() calls
#   load     interrupt load on the receiver (ISR us every 256us)
#   foreign  half of the packets addressed to another device
#   gap      busy receiver, packets every GAP_MS (10)
#   cpu      interrupt time of W (own frames, other device's frames, edges)
#   millis   millis() drift of the receiver
# Environment: COUNT, MODES, BUSY, ISR
cd "$(dirname "$0")"
mkdir -p build
if [ ! -d build/orig_src ]; then
  mkdir -p build/orig_src
  git -C "$(git rev-parse --show-toplevel)" archive "${ORIG_REF:-master}" src \
    | tar -x -C build/orig_src --strip-components=1 || exit 2
fi
flags() {
  case $1 in
    O) echo "-Ibuild/orig_src";;
    P) echo "";;
    F) echo "-DSWBB_PIN=2";;
    I) echo "-DSWBB_PIN=2 -DSWBB_INTERRUPT";;
    IA) echo "-DSWBB_PIN=2 -DSWBB_INTERRUPT -DSWBB_INTERRUPT_ACK";;
    W) echo "-DSWBB_PIN=2 -DSWBB_INTERRUPT_WAKE";;
  esac
}
run() { # run <label> <A flags> <B flags>
  local out
  out=$(./run_swbb_tests.sh "$2" "$3" 2>&1)
  printf "%-36s ack %4s  rx %-46s %9s us  contention %s\n" "$1" \
    "$(echo "$out" | sed -n 's/^A: DONE ok \([0-9]*\) fail.*/\1/p')" \
    "$(echo "$out" | sed -n 's/^B: DONE received \(.*\)/\1/p')" \
    "$(echo "$out" | sed -n 's/^A: DONE.* us \([0-9]*\).*/\1/p')" \
    "$(echo "$out" | sed -n 's/.*contention events: \([0-9]*\).*/\1/p')"
  echo "$out" | grep -i "error" | head -3
}
section=${1:-matrix}
case $section in
  pair)
    m=${4:-3}
    run "mode $m $2->$3" "$(flags $2) -DSWBB_MODE=$m" "$(flags $3) -DSWBB_MODE=$m";;
  matrix)
    for m in ${MODES:-1 2 3 4 5 6}; do
      if [ $m -le 4 ]; then tx="O P F IA W"; rx="O P F W"; else tx="F W"; rx="F W"; fi
      for a in $tx; do
        run "mode $m $a->W" "$(flags $a) -DSWBB_MODE=$m" "$(flags W) -DSWBB_MODE=$m"
      done
      for b in $rx; do
        [ $b = W ] && continue
        run "mode $m W->$b" "$(flags W) -DSWBB_MODE=$m" "$(flags $b) -DSWBB_MODE=$m"
      done
    done;;
  busy)
    for m in ${MODES:-3}; do
      for busy in ${BUSY:-0 200 500 1000 3000 10000}; do
        for r in F IA W; do
          [ $m -gt 4 ] && [ $r = IA ] && continue
          run "mode $m busy $busy F->$r" "$(flags F) -DSWBB_MODE=$m" \
            "$(flags $r) -DSWBB_MODE=$m -DBUSY_US=$busy -DRX_TIMEOUT=8000"
        done
      done
    done;;
  gap)
    # As busy, with GAP_MS (10) between packets: a link that is not saturated
    for m in ${MODES:-3}; do
      for busy in ${BUSY:-1000 3000 10000}; do
        for r in F IA W; do
          SIM_TIME=30 run "mode $m gap ${GAP_MS:-10}ms busy $busy F->$r" \
            "$(flags F) -DSWBB_MODE=$m -DTX_GAP_MS=${GAP_MS:-10}" \
            "$(flags $r) -DSWBB_MODE=$m -DBUSY_US=$busy -DRX_TIMEOUT=25000"
        done
      done
    done;;
  load)
    for m in ${MODES:-1 3 5}; do
      for r in F IA W; do
        [ $m -gt 4 ] && [ $r = IA ] && continue
        for isr in ${ISR:-5 10 15 25 40}; do
          run "mode $m F->$r rx ISR ${isr}us" "$(flags F) -DSWBB_MODE=$m" \
            "$(flags $r) -DSWBB_MODE=$m -DLOAD_ISR=$isr"
        done
      done
    done;;
  foreign)
    for m in ${MODES:-3 5}; do
      for r in F W; do
        run "mode $m F->$r foreign" "$(flags F) -DSWBB_MODE=$m -DFOREIGN" \
          "$(flags $r) -DSWBB_MODE=$m -DFOREIGN"
      done
    done;;
  cpu)
    # Interrupt time of W: COUNT packets to W, each preceded by one to
    # another device (debug pin trace)
    for m in ${MODES:-3}; do
      SIM_FLAGS=-e ./run_swbb_tests.sh "$(flags F) -DSWBB_MODE=$m -DFOREIGN" \
        "$(flags W) -DSWBB_MODE=$m -DSWBB_IRQ_PIN_DEBUG" > build/wake_cpu.txt
      echo "mode $m, ${COUNT:-200} packets to W and ${COUNT:-200} to another device:"
      python3 wake_isr.py build/wake_cpu.txt | sed 's/^/  /'
    done;;
  millis)
    # millis() of the receiver when it prints DONE vs the simulated time
    for m in ${MODES:-1 3 6}; do
      for r in F W; do
        out=$(SIM_FLAGS=-T ./run_swbb_tests.sh "$(flags F) -DSWBB_MODE=$m" \
          "$(flags $r) -DSWBB_MODE=$m -DBUSY_US=100 -DRX_TIMEOUT=8000" 2>&1)
        echo "mode $m F->$r: $(echo "$out" | grep '^B: DONE' | \
          sed 's/.*received \([0-9]*\).* ms \([0-9]*\).*\[sim \([0-9.]*\) ms\]/received \1 millis \2 simulated \3 ms/')"
      done
    done;;
esac
