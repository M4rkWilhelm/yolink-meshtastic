#!/usr/bin/env bash
# Build the packet encoder for the host and verify its output in Python.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=.pio/host_test
mkdir -p "$OUT"
C=lib/CryptoAesCtr/src
g++ -std=c++17 -O1 -Wall -I"$C" -o "$OUT/packet_test" \
    test/packet_test.cpp src/meshtastic_packet.cpp \
    "$C/AES128.cpp" "$C/AES256.cpp" "$C/AESCommon.cpp" "$C/BlockCipher.cpp" "$C/Cipher.cpp" \
    "$C/CTR.cpp" "$C/Crypto.cpp"
"$OUT/packet_test" | "$(cd .. && pwd)/.venv/bin/python" -I test/check_packet.py
