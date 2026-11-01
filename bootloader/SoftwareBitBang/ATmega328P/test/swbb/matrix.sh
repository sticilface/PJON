#!/bin/bash
# Runs the link test for every mode and combination of implementations
# Usage: ./matrix.sh [modes]   prints: mode tx->rx acks received time contention
# O = original library (git ORIG_REF, default master), P = this library
# (portable implementation), F = this library with SWBB_PIN (direct port
# access). Environment: COUNT, LENGTH, BUS_PIN, EXTRA (flags for both nodes,
# e.g. EXTRA=-DLOAD_ISR=15 to add interrupt load)
cd "$(dirname "$0")"
# Original library: src of ORIG_REF (default master) extracted in build/
if [ ! -d build/orig_src ]; then
  mkdir -p build/orig_src
  git -C "$(git rev-parse --show-toplevel)" archive "${ORIG_REF:-master}" src | tar -x -C build/orig_src \
    --strip-components=1 || exit 2
fi
O="-Ibuild/orig_src"     # original library (ORIG_REF)
P=""                     # this library, portable implementation
F="-DSWBB_PIN=${BUS_PIN:-2}"  # this library, direct port access
for m in ${*:-1 2 3 4}; do
  for pair in "O O" "P P" "F F" "O P" "P O" "O F" "F O" "P F" "F P"; do
    set -- $pair
    eval "a=\$$1"; eval "b=\$$2"
    out=$(./run_swbb_tests.sh "$a -DSWBB_MODE=$m ${EXTRA:-}" \
      "$b -DSWBB_MODE=$m ${EXTRA:-}" 2>&1)
    ok=$(echo "$out" | sed -n 's/^A: DONE ok \([0-9]*\) fail.*/\1/p')
    us=$(echo "$out" | sed -n 's/^A: DONE.* us \([0-9]*\).*/\1/p')
    rx=$(echo "$out" | sed -n 's/^B: DONE received \([0-9]*\) bad \([0-9]*\).*/\1 bad \2/p')
    ct=$(echo "$out" | sed -n 's/.*contention events: \([0-9]*\).*/\1/p')
    printf "mode %s  %s->%s  ack %4s  rx %-10s  %8s us  contention %s\n" \
      $m $1 $2 "$ok" "$rx" "$us" "$ct"
  done
done
