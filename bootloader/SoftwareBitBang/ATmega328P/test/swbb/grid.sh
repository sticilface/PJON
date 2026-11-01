#!/bin/bash
# Explores the timing of a fast mode (mode 5 with overridden values):
# ./grid.sh <tests> "<bit pad acc>" ...
#   tests: comma separated list of
#     ff    SWBB_PIN <-> SWBB_PIN
#     load  SWBB_PIN <-> SWBB_PIN, interrupt load (LOAD_ISR=15) on both
#     bl    SWBB_PIN -> bootloader at 16MHz
#     bl8   SWBB_PIN -> bootloader at 8MHz
#     tx-N/tx+N  as ff with the transmitter bit N% shorter / longer
#     bl-N/bl+N  as bl with the transmitter bit N% shorter / longer
# Prints the packets acknowledged at the first attempt out of COUNT (50).
cd "$(dirname "$0")"
TESTS=$1
shift
export COUNT=${COUNT:-50}
for v in "$@"; do
  set -- $v
  B=$1; P=$2; A=$3
  LIB="-DSWBB_PIN=2 -DSWBB_MODE=5 -DSWBB_MODE5_BIT=$B -DSWBB_MODE5_PAD=$P -DSWBB_MODE5_ACC=$A"
  BL="-DMODE5_BIT_US=$B -DMODE5_PAD_US=$P -DMODE5_ACC_US=$A"
  line="bit $B pad $P acc $A:"
  for t in ${TESTS//,/ }; do
    case $t in
      ff) r=$(./run_swbb_tests.sh "$LIB" "$LIB" 2>/dev/null);;
      load) r=$(./run_swbb_tests.sh "$LIB -DLOAD_ISR=15" "$LIB -DLOAD_ISR=15" \
        2>/dev/null);;
      bl) r=$(BL_FLAGS="$BL" ./run_boot_link.sh 5 "$LIB" 2>/dev/null);;
      bl8) r=$(BL_FLAGS="$BL" ./run_boot_link.sh 5 "$LIB" 8000000 2>/dev/null);;
      tx-*|tx+*) # transmitter bit = bit +- percentage, e.g. tx-5 tx+5
        b=$(awk -v b=$B -v p=${t#tx} 'BEGIN { print b * (1 + p / 100) }')
        r=$(./run_swbb_tests.sh "$LIB -DSWBB_TX_BIT=$b" "$LIB" 2>/dev/null);;
      b8-*|b8+*) # same with the bootloader at 8MHz
        b=$(awk -v b=$B -v p=${t#b8} 'BEGIN { print b * (1 + p / 100) }')
        r=$(BL_FLAGS="$BL" ./run_boot_link.sh 5 "$LIB -DSWBB_TX_BIT=$b" \
          8000000 2>/dev/null);;
      bl-*|bl+*) # same with the bootloader as receiver
        b=$(awk -v b=$B -v p=${t#bl} 'BEGIN { print b * (1 + p / 100) }')
        r=$(BL_FLAGS="$BL" ./run_boot_link.sh 5 "$LIB -DSWBB_TX_BIT=$b" \
          2>/dev/null);;
    esac
    ok=$(echo "$r" | sed -n 's/^A: DONE ok \([0-9]*\).*/\1/p')
    us=$(echo "$r" | sed -n 's/^A: DONE.* us \([0-9]*\).*/\1/p')
    line="$line $t=${ok:-x}"
    [ "$t" = ff ] && line="$line (${us}us)"
  done
  echo "$line"
done
