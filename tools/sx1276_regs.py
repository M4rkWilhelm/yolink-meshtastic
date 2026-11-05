#!/usr/bin/env python3
"""Read SX1276 registers over SWD by driving the STM32's SPI2 directly from
the debugger (CPU halted). Needs our firmware running (SPI2 already set up)
and OpenOCD from tools/connect.sh.

Usage: sx1276_regs.py [reg ...]   (hex register numbers; default: a useful set)
"""
import socket
import sys

SPI2 = 0x40003800          # CR1 +0x00, SR +0x08, DR +0x0C
GPIOB_BSRR = 0x50000418    # PB12 = radio NSS
NSS = 1 << 12

NAMES = {
    0x01: "RegOpMode", 0x06: "RegFrfMsb", 0x07: "RegFrfMid", 0x08: "RegFrfLsb",
    0x09: "RegPaConfig", 0x0B: "RegOcp", 0x0C: "RegLna", 0x1D: "RegModemConfig1",
    0x1E: "RegModemConfig2", 0x20: "RegPreambleMsb", 0x21: "RegPreambleLsb",
    0x26: "RegModemConfig3", 0x39: "RegSyncWord", 0x40: "RegDioMapping1",
    0x42: "RegVersion", 0x4B: "RegTcxo", 0x4D: "RegPaDac",
}


class Ocd:
    def __init__(self):
        self.s = socket.create_connection(("localhost", 6666), timeout=20)

    def cmd(self, c):
        self.s.sendall((c + "\x1a").encode())
        b = b""
        while not b.endswith(b"\x1a"):
            b += self.s.recv(4096)
        return b[:-1].decode(errors="replace").strip()

    def rd(self, addr):
        return int(self.cmd(f"read_memory 0x{addr:08X} 32 1"), 0)

    def wr(self, addr, val):
        self.cmd(f"write_memory 0x{addr:08X} 32 {{0x{val:X}}}")

    def wr8(self, addr, val):
        self.cmd(f"write_memory 0x{addr:08X} 8 {{0x{val:X}}}")


def xfer(ocd, byte):
    # A byte at 4 MHz finishes long before the debugger's next access, so no
    # need to poll TXE/RXNE (polling also hangs: RXNE reads back clear).
    ocd.wr8(SPI2 + 0x0C, byte)
    return ocd.rd(SPI2 + 0x0C) & 0xFF


def read_reg(ocd, reg):
    ocd.wr(GPIOB_BSRR, NSS << 16)  # NSS low
    xfer(ocd, reg & 0x7F)
    val = xfer(ocd, 0x00)
    ocd.wr(GPIOB_BSRR, NSS)        # NSS high
    return val


def main():
    regs = [int(r, 16) for r in sys.argv[1:]] or sorted(NAMES)
    ocd = Ocd()
    ocd.cmd("halt")
    cr1 = ocd.rd(SPI2)
    if not cr1 & 0x40:
        sys.exit(f"SPI2 not enabled (CR1={cr1:#x}); is our firmware running?")
    for r in regs:
        print(f"0x{r:02X} {NAMES.get(r, ''):16s} 0x{read_reg(ocd, r):02X}")
    ocd.cmd("resume")


if __name__ == "__main__":
    main()
