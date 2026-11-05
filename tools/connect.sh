#!/usr/bin/env bash
# Start OpenOCD against the YoLink board and arm "halt on reset".
# The clone ST-Link's RST pin is dead, so reset is held by hand:
#   1. Jumper header pad 5 (nRST) to pad 4 (GND).
#   2. Run this script (it stays running; logs to openocd_session.log).
#   3. When it prints "ARMED", remove the jumper. The core halts at 0x080000D4.
cd "$(dirname "$0")/.."
pkill -x openocd 2>/dev/null && sleep 1
# Pull the jumper off cleanly: if the link glitches and the chip resets again
# before we flash, the stock bootloader can switch SWD off and you start over.
# Dual-bank config: the L073 has two 96 KB flash banks; stm32l0.cfg only
# declares the first, so images over 96 KB would be silently truncated.
openocd -f interface/stlink.cfg -f target/stm32l0_dual_bank.cfg \
  -c "adapter speed 300" -c "reset_config none" -c "init" \
  -c "mww 0xE000EDFC 0x01000001" -c "mdw 0xE000EDFC 1" \
  -c "echo ARMED" > openocd_session.log 2>&1 &
for _ in $(seq 20); do
  grep -q ARMED openocd_session.log && { echo "ARMED - remove the jumper now"; exit 0; }
  grep -q "Error" openocd_session.log && break
  sleep 0.5
done
echo "OpenOCD did not connect; is the jumper on pad 5 -> pad 4?"; grep -E "Error|Info : Target" openocd_session.log
exit 1
