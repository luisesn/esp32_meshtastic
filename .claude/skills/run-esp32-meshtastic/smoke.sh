#!/usr/bin/env bash
# Build smoke test for esp32_meshtastic firmware.
# Runs idf.py build, verifies the binary exists and is non-trivially sized.
# Exit 0 = build good.  Exit 1 = something broke.

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

echo "=== Building esp32_meshtastic ==="
idf.py build

BIN=build/esp32_meshtastic.bin
ELF=build/esp32_meshtastic.elf

if [[ ! -f "$BIN" ]]; then
  echo "ERROR: $BIN not found after build" >&2; exit 1
fi
if [[ ! -f "$ELF" ]]; then
  echo "ERROR: $ELF not found after build" >&2; exit 1
fi

SIZE=$(stat -c%s "$BIN")
if (( SIZE < 50000 )); then
  echo "ERROR: binary looks too small ($SIZE bytes — expected >50 KB)" >&2; exit 1
fi

echo ""
echo "=== Build OK ==="
echo "  $(basename $BIN)  $(( SIZE / 1024 )) KB"
idf.py size 2>&1 | grep -E "IRAM|DRAM|Flash" | head -6
echo ""
echo "Flash layout (from flash_args):"
cat build/flash_args
