#!/bin/bash
# Link tests on the analog bus model (swbb_sim -C/-R/-P): bus capacitance
# CAP (pF, default 1500 ~ 20m of untwisted cable + 15 devices), series
# resistor 100 ohm, pull-down PULL (default 100000).
# Usage: ./rc_eval.sh [matrix|fast|boot]   prints one line per test
cd "$(dirname "$0")"
CAP=${CAP:-1500}
PULL=${PULL:-100000}
export SIM_FLAGS="-C $CAP -R 100 -P $PULL"
flags() {
  case $1 in
    O) echo "-Ibuild/orig_src";; P) echo "";; F) echo "-DSWBB_PIN=2";;
    I) echo "-DSWBB_PIN=2 -DSWBB_INTERRUPT -DSWBB_INTERRUPT_ACK";;
    W) echo "-DSWBB_PIN=2 -DSWBB_INTERRUPT_WAKE";;
  esac
}
link() { # link <mode> <tx> <rx>
  local out
  out=$(COUNT=${COUNT:-100} ./run_swbb_tests.sh "$(flags $2) -DSWBB_MODE=$1" \
    "$(flags $3) -DSWBB_MODE=$1" 2>&1)
  printf "C=%spF pull=%s mode %s %s->%s: ack %s/%s contention %s\n" $CAP $PULL \
    $1 $2 $3 "$(echo "$out" | sed -n 's/^A: DONE ok \([0-9]*\).*/\1/p')" \
    ${COUNT:-100} "$(echo "$out" | sed -n 's/.*contention events: \([0-9]*\).*/\1/p')"
}
case ${1:-matrix} in
  matrix)
    for m in 1 2 3 4; do
      for p in "O O" "P P" "F F" "O F" "F O" "P F" "F P" "F W" "W F" "F I" "O W"
      do link $m $p; done
    done;;
  fast)
    for m in 5 6; do for p in "F F" "F W" "W F"; do link $m $p; done; done;;
  boot)
    for m in 1 3 4 5 6; do
      for u in F P; do
        [ $u = P ] && [ $m -gt 4 ] && continue
        echo "C=${CAP}pF pull=$PULL bootloader mode $m uploader $u: $(SIM_TIME=30 \
          ./run_boot_link.sh $m "$(flags $u)" 2>&1 | grep -m1 'A: DONE')"
      done
    done;;
esac
