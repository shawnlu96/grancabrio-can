#!/bin/bash
# Laptop tests for the board's pure logic. No board, no car needed.
#   candec_test     decoding + vehicle state (trip unwrap, staleness)
#   canlog_test     raw frame stream format (ESP-IDF UART replaced by test/stub)
#   drive_test      performance timing / gear changes / wheel speeds, then replay of the samples
#   dbc_crosscheck  DBC file and candec.h decode both sample logs identically (needs cantools)
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
CXX="c++ -std=c++17 -I.. -O0 -Wall -Wextra"
$CXX -o "$T/candec_test" candec_test.cpp && "$T/candec_test"
$CXX -Istub -o "$T/canlog_test" canlog_test.cpp && "$T/canlog_test"
$CXX -o "$T/drive_test" drive_test.cpp
$CXX -o "$T/decode_dump" decode_dump.cpp
for s in ../../samples/*.log.gz; do gzip -dc "$s" > "$T/$(basename "$s" .gz)"; done
"$T/drive_test" "$T"/*.log | grep -v '^G '
if python3 -c 'import cantools' 2>/dev/null; then
  for l in "$T"/*.log; do
    echo "== DBC vs firmware: $(basename "$l")"
    "$T/decode_dump" < "$l" > "$l.fw"
    python3 dbc_crosscheck.py ../../dbc/maserati_grancabrio_2016.dbc "$l" "$l.fw" | tail -1
  done
else
  echo "(skipping DBC cross-check: pip install cantools)"
fi
