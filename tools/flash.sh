#!/usr/bin/env bash
# Flash a firmware image (.elf, or .bin at 0x08000000) to the YoLink board
# through an OpenOCD started by tools/connect.sh, then run it.
#
#   Stock YoLink firmware on the chip: SWD is off once it boots, so run
#   connect.sh with the reset jumper first (pull it when it says ARMED).
#   Our firmware on the chip: SWD stays on; connect.sh works without the jumper.
#
# Usage: tools/flash.sh path/to/firmware.elf|bin
#        tools/flash.sh dumps/ys7805_flash_stock.bin   # restore stock firmware
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="$(realpath "$1")"
OCD=("$ROOT/.venv/bin/python" -I "$ROOT/tools/ocd.py")

if ! pgrep -x openocd >/dev/null; then
  echo "OpenOCD isn't running. Run tools/connect.sh first (with the reset jumper"
  echo "if the stock firmware is on the chip)."
  exit 1
fi

# OpenOCD's `program` starts with a reset, which fails here (no working reset
# line, and the stock firmware disables SWD as soon as it runs). The core is
# already halted, so write and verify directly instead.
case "$IMG" in
  *.elf) WRITE="flash write_image erase {$IMG}"; VERIFY="verify_image {$IMG}" ;;
  *.bin) WRITE="flash write_image erase {$IMG} 0x08000000"; VERIFY="verify_image {$IMG} 0x08000000" ;;
  *) echo "expected a .elf or .bin file"; exit 1 ;;
esac

# Must be halted with working memory access (curstate alone can be stale).
# A busy or just-woken chip can take a while to halt, so retry before writing:
# writing to a running chip fails part-way ("timed out while waiting for target halted").
# Our firmware sleeps in true Stop mode, where the debugger can't reach the core,
# and wakes at least every 10 s, so keep trying for a while.
# "Halted" isn't enough: halted inside Stop, the system clocks are off and every
# read returns 0 (and flash writes fail). Wait until the chip ID reads back.
awake() { [ "$("${OCD[@]}" "read_memory 0x40015800 32 1" 2>/dev/null)" = "0x20086447" ]; }
for _ in $(seq 1 150); do
  "${OCD[@]}" "halt 300" >/dev/null 2>&1 || true
  [ "$("${OCD[@]}" "stm32l0.cpu curstate" 2>/dev/null)" = "halted" ] && awake && break
  "${OCD[@]}" "resume" >/dev/null 2>&1 || true
  sleep 0.3
done
# Keep the debugger able to reach the chip for the rest of this run: set
# DBGMCU_CR DBG_SLEEP|DBG_STOP|DBG_STANDBY. Our firmware clears them at boot.
"${OCD[@]}" "mmw 0x40021034 0x00400000 0" "mww 0x40015804 0x7" >/dev/null 2>&1 || true
if [ "$("${OCD[@]}" "stm32l0.cpu curstate" 2>/dev/null)" != "halted" ] || ! awake; then
  echo "Couldn't halt the chip. Redo tools/connect.sh with the reset jumper, then run this again."
  exit 1
fi
if ! "${OCD[@]}" "read_memory 0x40015800 32 1" >/dev/null 2>&1; then
  echo "Can't read the chip's memory: the SWD link is down. Redo tools/connect.sh"
  echo "with the reset jumper (pull it when it says ARMED), then run this again."
  exit 1
fi
# Our firmware runs a ~25 s watchdog (IWDG) that keeps counting while the core
# is halted unless the debug unit freezes it, and a reset mid-write would leave
# a half-written image. Freeze it now: enable the DBGMCU clock (RCC_APB2ENR
# DBGEN), then set DBGMCU_APB1FZ DBG_IWDG_STOP. Harmless for stock firmware.
"${OCD[@]}" "mmw 0x40021034 0x00400000 0" "mmw 0x40015808 0x00001000 0" >/dev/null
# Clear sticky FLASH_SR error flags (WRPERR, PGAERR, SIZERR, OPTVERR, RDERR,
# NOTZEROERR, FWWERR; write 1 to clear). Left set by an earlier failed write,
# they make every later erase fail ("invalid program address"), and OpenOCD's
# stm32lx driver doesn't clear them itself.
"${OCD[@]}" "write_memory 0x40022018 32 {0x32F00}" >/dev/null 2>&1 || true
echo "programming $IMG ..."
# The first write after the chip was caught often fails ("invalid program
# address": OpenOCD's RAM loader fails, its fallback hits a programming error).
# A second attempt, after clearing the error flags, has always worked.
if ! "${OCD[@]}" "$WRITE"; then
  echo "write failed; clearing flash errors and retrying once"
  "${OCD[@]}" "halt 500" "write_memory 0x40022018 32 {0x32F00}" >/dev/null 2>&1 || true
  "${OCD[@]}" "$WRITE"
fi
# verify_image prints "verified N bytes" only on a full match; anything else
# (e.g. a bank OpenOCD doesn't know about) must fail loudly.
VERIFY_OUT="$("${OCD[@]}" "$VERIFY" 2>&1 || true)"
echo "$VERIFY_OUT" | grep -v '^\s*$'
if ! grep -q "^verified" <<<"$VERIFY_OUT"; then
  echo "VERIFY FAILED: flash does not match $IMG"
  exit 1
fi
# Drop the halt-on-reset vector catch, then restart via SYSRESETREQ (AIRCR).
# Avoids OpenOCD's `reset`, which doesn't work on this setup.
"${OCD[@]}" "mww 0xE000EDFC 0x00000000" "mww 0xE000ED0C 0x05FA0004" || true
echo "flashed and running"
