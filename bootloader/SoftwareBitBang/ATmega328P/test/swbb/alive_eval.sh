#!/bin/bash
# SWBB_WAKE_ALIVE_MS: a wake receiver must stop acknowledging when its
# application stops calling bus.receive().
# 1. stuck: B stops calling bus.receive() after 500ms, A sends a packet every
#    ~10ms for ~2s: packets acknowledged after the stop must be <= the
#    queue size (frames are acknowledged only while bus.receive() runs)
# 2. busy: B calls bus.receive() every BUSY_US: acknowledged while BUSY_US is
#    shorter than SWBB_WAKE_ALIVE_MS
# 3. heartbeat (SWBB_WAKE_HEARTBEAT): B keeps calling bus.receive() but stops
#    calling bus.strategy.alive() after 500ms: acknowledgements must stop
# Usage: ./alive_eval.sh [modes]   LIB=<src dir> to test another library
cd "$(dirname "$0")"
I=${LIB:+-I$LIB}
W="$I -DSWBB_PIN=2 -DSWBB_INTERRUPT_WAKE"
for m in ${*:-3 7}; do
  out=$(COUNT=150 ./run_swbb_tests.sh "$I -DSWBB_PIN=2 -DSWBB_MODE=$m -DTX_GAP_MS=10" \
    "$W -DSWBB_MODE=$m -DRX_STOP_MS=500 -DRX_TIMEOUT=2500" 2>&1)
  echo "mode $m stuck after 500ms: $(echo "$out" | grep '^[AB]: DONE' | tr '\n' ' ')"
  out=$(COUNT=150 ./run_swbb_tests.sh "$I -DSWBB_PIN=2 -DSWBB_MODE=$m -DTX_GAP_MS=10" \
    "$W -DSWBB_MODE=$m -DRX_STOP_MS=500 -DRX_TIMEOUT=2500 -DRX_STUCK_EMPTY" 2>&1)
  echo "mode $m stuck after 500ms, queue looks empty: $(echo "$out" | grep '^[AB]: DONE' | tr '\n' ' ')"
  out=$(COUNT=150 ./run_swbb_tests.sh "$I -DSWBB_PIN=2 -DSWBB_MODE=$m -DTX_GAP_MS=10" \
    "$W -DSWBB_MODE=$m -DSWBB_WAKE_HEARTBEAT -DRX_TIMEOUT=2500" 2>&1)
  echo "mode $m heartbeat, alive() called: $(echo "$out" | grep '^[AB]: DONE' | tr '\n' ' ')"
  out=$(COUNT=150 ./run_swbb_tests.sh "$I -DSWBB_PIN=2 -DSWBB_MODE=$m -DTX_GAP_MS=10" \
    "$W -DSWBB_MODE=$m -DSWBB_WAKE_HEARTBEAT -DRX_HEARTBEAT_STOP_MS=500 -DRX_TIMEOUT=2500" 2>&1)
  echo "mode $m heartbeat, alive() stops after 500ms, bus.receive() goes on: $(echo "$out" | grep '^[AB]: DONE' | tr '\n' ' ')"
  out=$(COUNT=150 ./run_swbb_tests.sh "$I -DSWBB_PIN=2 -DSWBB_MODE=$m -DTX_GAP_MS=10" \
    "$W -DSWBB_MODE=$m -DRX_HEARTBEAT_STOP_MS=500 -DRX_TIMEOUT=2500" 2>&1)
  echo "mode $m no heartbeat, same application: $(echo "$out" | grep '^[AB]: DONE' | tr '\n' ' ')"
  for busy in 20000 50000 150000; do
    out=$(COUNT=20 ./run_swbb_tests.sh "$I -DSWBB_PIN=2 -DSWBB_MODE=$m -DTX_GAP_MS=20" \
      "$W -DSWBB_MODE=$m -DBUSY_US=$busy -DRX_TIMEOUT=6000" 2>&1)
    echo "mode $m busy ${busy}us: $(echo "$out" | grep '^A: DONE' | sed 's/ us .*//')"
  done
done
