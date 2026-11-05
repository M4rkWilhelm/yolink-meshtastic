#!/usr/bin/env python3
"""Run the stock YoLink firmware under OpenOCD while keeping SWD alive,
and log motion / button events.

Prerequisite: OpenOCD running with the chip halted at reset (see probe notes):
  jumper pad5->pad4, start openocd (reset_config none),
  mww 0xE000EDFC 0x01000001, remove jumper.

Talks to OpenOCD's Tcl port (6666). Ctrl-C to stop (the chip is left running).
"""
import argparse
import re
import socket
import time

# Stock firmware addresses (ys7805_flash_stock.bin, sha256 d256d5d4...)
SKIP_BOOT_GPIOA = 0x08000B06    # bootloader: str 0xFFFFFFFF -> GPIOA_MODER (all analog, kills SWD)
SKIP_BOOT_NEXT = 0x08000B08
SKIP_LPUART_PINS = 0x08006B54   # bl HAL_GPIO_Init(GPIOA, PA13|PA14, AF6 LPUART1)
SKIP_LPUART_NEXT = 0x08006B58
PIR_HANDLER = 0x08008E90        # PB6 EXTI callback -> event 0x10
BUTTON_HANDLER = 0x08007C40     # PA2 EXTI callback -> event 0x04
RESET_VECTOR = 0x080000D4       # boot Reset_Handler (vector catch stops here)


class Ocd:
    def __init__(self, port):
        self.s = socket.create_connection(("localhost", port), timeout=30)

    def cmd(self, c):
        self.s.sendall(f"capture {{{c}}}\x1a".encode())
        buf = b""
        while not buf.endswith(b"\x1a"):
            buf += self.s.recv(4096)
        return buf[:-1].decode(errors="replace").strip()

    def reg(self, name):
        m = re.search(r"0x([0-9a-fA-F]+)", self.cmd(f"reg {name}"))
        return int(m.group(1), 16)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=6666)
    ap.add_argument("--minutes", type=float, default=10)
    args = ap.parse_args()

    ocd = Ocd(args.port)
    ocd.cmd("halt")
    pc = ocd.reg("pc") if "0x" in ocd.cmd("reg pc") else None
    if pc != RESET_VECTOR:
        raise SystemExit(f"chip is not halted at reset (pc={pc and hex(pc)}); redo the jumper procedure")
    # Keep the debug block clocked through Sleep/Stop/Standby.
    ocd.cmd("mww 0x40021034 [expr {[mrw 0x40021034] | (1 << 22)}]")  # RCC_APB2ENR.DBGEN
    ocd.cmd("mww 0x40015804 0x7")                                    # DBGMCU_CR
    # Cortex-M0+ has exactly 4 hardware breakpoints.
    for addr in (SKIP_BOOT_GPIOA, SKIP_LPUART_PINS, PIR_HANDLER, BUTTON_HANDLER):
        ocd.cmd(f"rbp 0x{addr:08X}")
        print(ocd.cmd(f"bp 0x{addr:08X} 2 hw") or f"bp 0x{addr:08X} set")
    ocd.cmd("resume")
    t0 = time.time()
    print(f"running; watching for {args.minutes} min. Wave at the sensor / press its button.")

    try:
        while time.time() - t0 < args.minutes * 60:
            state = ocd.cmd("stm32l0.cpu curstate")
            if state != "halted":
                time.sleep(0.1)
                continue
            pc = ocd.reg("pc")
            ts = f"[{time.time() - t0:7.1f}s]"
            if pc == SKIP_BOOT_GPIOA:
                print(f"{ts} bootloader tried to set all GPIOA pins analog -> skipped (SWD kept)")
                ocd.cmd(f"reg pc 0x{SKIP_BOOT_NEXT:08X}")
            elif pc == SKIP_LPUART_PINS:
                print(f"{ts} firmware tried to give PA13/PA14 to LPUART1 -> skipped (SWD kept)")
                ocd.cmd(f"reg pc 0x{SKIP_LPUART_NEXT:08X}")
            elif pc == PIR_HANDLER:
                print(f"{ts} MOTION (PB6 interrupt)")
            elif pc == BUTTON_HANDLER:
                print(f"{ts} BUTTON (PA2 interrupt)")
            elif pc == RESET_VECTOR:
                print(f"{ts} chip reset (watchdog or firmware reset); re-arming")
                ocd.cmd("mww 0x40021034 [expr {[mrw 0x40021034] | (1 << 22)}]")
                ocd.cmd("mww 0x40015804 0x7")
            else:
                print(f"{ts} halted at unexpected pc 0x{pc:08X}")
            ocd.cmd("resume")
    except KeyboardInterrupt:
        pass
    finally:
        print("stopping watch; chip left running")


if __name__ == "__main__":
    main()
