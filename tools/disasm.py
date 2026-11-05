#!/usr/bin/env python3
"""Disassemble an address range of the STM32L0 flash dump (Thumb).

Usage: disasm.py START END [--bin dumps/ys7805_flash_stock.bin]
Literal-pool loads are annotated with the loaded value and a peripheral name.
"""
import argparse
import struct

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

BASE = 0x08000000

# STM32L0x3 peripheral bases (RM0367), for annotating literal loads.
PERIPHS = {
    0x40000000: "TIM2", 0x40000400: "TIM3", 0x40001000: "TIM6", 0x40001400: "TIM7",
    0x40002800: "RTC", 0x40002C00: "WWDG", 0x40003000: "IWDG", 0x40003800: "SPI2",
    0x40004400: "USART2", 0x40004800: "LPUART1", 0x40004C00: "USART4", 0x40005000: "USART5",
    0x40005400: "I2C1", 0x40005800: "I2C2", 0x40007000: "PWR", 0x40007800: "I2C3",
    0x40007C00: "LPTIM1", 0x40010000: "SYSCFG", 0x40010200: "COMP", 0x40010400: "EXTI",
    0x40010800: "TIM21", 0x40010C00: "TIM22", 0x40011400: "FIREWALL", 0x40012400: "ADC",
    0x40013000: "SPI1", 0x40013800: "USART1", 0x40015800: "DBGMCU", 0x40020000: "DMA1",
    0x40021000: "RCC", 0x40022000: "FLASH", 0x40023000: "CRC", 0x40024000: "TSC",
    0x40025000: "RNG", 0x40026000: "AES", 0x50000000: "GPIOA", 0x50000400: "GPIOB",
    0x50000800: "GPIOC", 0x50000C00: "GPIOD", 0x50001C00: "GPIOH",
    0xE000E000: "SCS", 0xE000ED00: "SCB", 0xE000E100: "NVIC",
}


def periph_name(v):
    for base, name in PERIPHS.items():
        if base <= v < base + 0x400:
            return f"{name}+0x{v - base:X}"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("start", type=lambda s: int(s, 0))
    ap.add_argument("end", type=lambda s: int(s, 0))
    ap.add_argument("--bin", default="dumps/ys7805_flash_stock.bin")
    args = ap.parse_args()

    data = open(args.bin, "rb").read()
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    md.detail = True
    code = data[args.start - BASE:args.end - BASE]
    for ins in md.disasm(code, args.start):
        note = ""
        if ins.mnemonic == "ldr" and "[pc," in ins.op_str:
            imm = ins.operands[1].mem.disp
            lit = ((ins.address + 4) & ~3) + imm
            (val,) = struct.unpack_from("<I", data, lit - BASE)
            pn = periph_name(val)
            note = f"  ; =0x{val:08X}" + (f" {pn}" if pn else "")
        print(f"0x{ins.address:08X}: {ins.mnemonic:8s} {ins.op_str}{note}")


if __name__ == "__main__":
    main()
