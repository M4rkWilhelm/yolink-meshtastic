#!/usr/bin/env python3
"""Manual SX1276 transmit test over SWD (drives SPI2 from the debugger with the
CPU halted). Sends one 1-byte LoRa packet with the current radio settings and
reports whether TxDone fires. Optionally sets RegTcxo first.

Needs our firmware running (SPI2 configured) and OpenOCD from tools/connect.sh.
Radiates briefly on whatever frequency/power the firmware configured.

Usage: sx1276_txtest.py [--tcxo | --xtal] [--pa1-low]
  --xtal     put RegTcxo back to crystal mode (0x09)
  --pa1-low  hold PA1 (antenna switch, stock "TX" level) low during the test
"""
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))  # run with python -I
from sx1276_regs import GPIOB_BSRR, NSS, Ocd, read_reg, xfer

GPIOA_BSRR = 0x50000018
PA1 = 1 << 1


def write_reg(ocd, reg, val):
    ocd.wr(GPIOB_BSRR, NSS << 16)
    xfer(ocd, reg | 0x80)
    xfer(ocd, val)
    ocd.wr(GPIOB_BSRR, NSS)


def main():
    ocd = Ocd()
    ocd.cmd("halt")
    if "--tcxo" in sys.argv:
        write_reg(ocd, 0x01, 0x88)        # LoRa sleep: RegTcxo only changes in sleep
        write_reg(ocd, 0x4B, 0x19)        # TcxoInputOn
        print(f"RegTcxo now 0x{read_reg(ocd, 0x4B):02X}")
    if "--xtal" in sys.argv:
        write_reg(ocd, 0x01, 0x88)
        write_reg(ocd, 0x4B, 0x09)
        print(f"RegTcxo now 0x{read_reg(ocd, 0x4B):02X}")
    pa1 = "low" if "--pa1-low" in sys.argv else "high"
    ocd.wr(GPIOA_BSRR, PA1 << 16 if pa1 == "low" else PA1)
    print(f"PA1 {pa1}")
    write_reg(ocd, 0x01, 0x89)            # LoRa standby
    time.sleep(0.05)
    write_reg(ocd, 0x12, 0xFF)            # clear IRQ flags
    write_reg(ocd, 0x0E, 0x00)            # FifoTxBaseAddr
    write_reg(ocd, 0x0D, 0x00)            # FifoAddrPtr
    write_reg(ocd, 0x00, 0x55)            # one payload byte
    write_reg(ocd, 0x22, 0x01)            # RegPayloadLength
    print(f"before TX: OpMode 0x{read_reg(ocd, 0x01):02X} IrqFlags 0x{read_reg(ocd, 0x12):02X}"
          f" Tcxo 0x{read_reg(ocd, 0x4B):02X} PaConfig 0x{read_reg(ocd, 0x09):02X}")
    write_reg(ocd, 0x01, 0x8B)            # LoRa TX
    t0 = time.time()
    while time.time() - t0 < 2.0:
        flags = read_reg(ocd, 0x12)
        if flags & 0x08:
            print(f"TxDone after ~{time.time() - t0:.2f}s, OpMode now 0x{read_reg(ocd, 0x01):02X}")
            break
    else:
        print(f"no TxDone after 2 s: OpMode 0x{read_reg(ocd, 0x01):02X} IrqFlags 0x{read_reg(ocd, 0x12):02X}")
    write_reg(ocd, 0x12, 0xFF)
    write_reg(ocd, 0x01, 0x89)
    ocd.wr(GPIOA_BSRR, PA1)               # back to the idle (RX) level
    ocd.cmd("resume")


if __name__ == "__main__":
    main()
