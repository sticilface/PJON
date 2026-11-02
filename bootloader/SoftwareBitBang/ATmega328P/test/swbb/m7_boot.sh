#!/bin/bash
# Mode 7 bootloader link: 200 write chunks (one attempt each) from a
# SWBB_PIN mode 7 uploader to the mode 7 bootloader, on the analog bus model
# Usage: ./m7_boot.sh "<bootloader F_CPU list>" "<M7_ADVANCE list>" [uploader flags]
cd "$(dirname "$0")"
for f in ${1:-16000000 8000000}; do
  for a in ${2:-25}; do
    r=$(BL_FLAGS="-DM7_ADVANCE=$a ${BLX:-}" \
      SIM_FLAGS="-C ${CAP:-1500} -R 100 -P 100000" \
      ./run_boot_link.sh 7 "-DSWBB_PIN=2 ${3:-}" $f 2>&1 | \
      grep -E -m1 "DONE|error")
    echo "bootloader ${f}Hz M7_ADVANCE $a [${3:-} ${BLX:-}]: $r"
  done
done
