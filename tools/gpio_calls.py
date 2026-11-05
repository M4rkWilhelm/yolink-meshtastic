#!/usr/bin/env python3
"""List every call to a GPIO helper in the stock firmware with best-effort
constant arguments (port, pin mask, value/init), recovered by simple forward
constant propagation over the instructions preceding each call.

Usage: gpio_calls.py [--bin dumps/ys7805_flash_stock.bin]
"""
import argparse
import struct

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG

BASE = 0x08000000
CODE_END = 0x08010B30
LOOKBACK = 24  # bytes of code examined before each call

HELPERS = {
    0x0800513E: "GPIO_Write(port, pin, val)",
    0x08006BE4: "GPIO_Init(port, pin, *init)",
    0x08006C50: "GPIO_Read(port, pin)",
    0x08004E90: "HAL_GPIO_DeInit(port, pin)",
    0x08004FA0: "HAL_GPIO_Init(port, *init)",
    0x08006C94: "GPIO_Write2(port, pin, val)",
    0x0800512E: "GPIO_Toggle(port, pin)",
}
PORTS = {0x50000000: "GPIOA", 0x50000400: "GPIOB", 0x50000800: "GPIOC",
         0x50000C00: "GPIOD", 0x50001C00: "GPIOH"}


def pins(mask):
    return "|".join(str(i) for i in range(16) if mask >> i & 1) or "?"


def resolve_args(md, data, call_addr):
    """Propagate constants through the LOOKBACK window ending at call_addr."""
    regs = {}
    start = call_addr - LOOKBACK
    for ins in md.disasm(data[start - BASE:call_addr - BASE], start):
        ops = ins.operands
        m = ins.mnemonic
        dst = ops[0].reg if ops and ops[0].type == ARM_OP_REG else None
        try:
            if m in ("movs", "mov") and ops[1].type == ARM_OP_IMM:
                regs[dst] = ops[1].imm
            elif m in ("movs", "mov") and ops[1].type == ARM_OP_REG:
                regs[dst] = regs.get(ops[1].reg)
            elif m == "lsls" and len(ops) == 3 and ops[2].type == ARM_OP_IMM:
                v = regs.get(ops[1].reg)
                regs[dst] = None if v is None else (v << ops[2].imm) & 0xFFFFFFFF
            elif m == "asrs" and len(ops) == 3 and ops[2].type == ARM_OP_IMM:
                v = regs.get(ops[1].reg)
                regs[dst] = None if v is None else v >> ops[2].imm
            elif m == "ldr" and ops[1].type == ARM_OP_MEM and ins.op_str.find("[pc") >= 0:
                lit = ((ins.address + 4) & ~3) + ops[1].mem.disp
                regs[dst] = struct.unpack_from("<I", data, lit - BASE)[0]
            elif m == "adds" and len(ops) == 2 and ops[1].type == ARM_OP_IMM:
                v = regs.get(dst)
                regs[dst] = None if v is None else v + ops[1].imm
            elif m in ("bl", "blx"):
                regs = {}  # call clobbers r0-r3; keep it simple
            elif dst is not None:
                regs[dst] = None
        except (IndexError, KeyError):
            if dst is not None:
                regs[dst] = None
    r = [regs.get(reg) for reg in (66, 67, 68)]  # ARM_REG_R0..R2 in capstone 5
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default="dumps/ys7805_flash_stock.bin")
    args = ap.parse_args()
    data = open(args.bin, "rb").read()
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    md.detail = True

    for off in range(0x1000, CODE_END - BASE - 4, 2):
        h1, h2 = struct.unpack_from("<HH", data, off)
        if not (h1 >> 11 == 0b11110 and h2 >> 14 == 0b11 and (h2 >> 12) & 1):
            continue
        ins = next(md.disasm(data[off:off + 4], BASE + off), None)
        if not ins or ins.mnemonic != "bl":
            continue
        target = int(ins.op_str.lstrip("#"), 16)
        if target not in HELPERS:
            continue
        r0, r1, r2 = resolve_args(md, data, BASE + off)
        port = PORTS.get(r0, "?" if r0 is None else hex(r0))
        pin = "?" if r1 is None else f"P{port[-1] if port.startswith('GPIO') else '?'}{pins(r1)}"
        val = "" if r2 is None else f" arg3={r2:#x}"
        print(f"0x{BASE + off:08X}  {HELPERS[target]:28s} {port:6s} {pin}{val}")


if __name__ == "__main__":
    main()
