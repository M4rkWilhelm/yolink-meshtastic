#!/usr/bin/env python3
"""Wrap a raw STM32L0 flash dump in a minimal ARM ELF so disassemblers that
need a header (e.g. Hopper) can load it at the right address.

Also emits FUNC symbols for every Cortex-M0+ / STM32L0 vector-table entry found
at the given table addresses, so interrupt handlers start out named.

Usage: bin2elf.py <in.bin> <out.elf> [--base 0x08000000] [--table NAME:ADDR ...]
"""
import argparse
import struct

CORE_VECTORS = [
    "initial_sp", "Reset_Handler", "NMI_Handler", "HardFault_Handler",
    None, None, None, None, None, None, None,
    "SVC_Handler", None, None, "PendSV_Handler", "SysTick_Handler",
]

# STM32L0x3 IRQ0..31 (RM0367)
STM32L0_IRQS = [
    "WWDG", "PVD", "RTC", "FLASH", "RCC_CRS", "EXTI0_1", "EXTI2_3", "EXTI4_15",
    "TSC", "DMA1_Channel1", "DMA1_Channel2_3", "DMA1_Channel4_7", "ADC1_COMP",
    "LPTIM1", "USART4_5", "TIM2", "TIM3", "TIM6_DAC", "TIM7", None, "TIM21",
    "I2C3", "TIM22", "I2C1", "I2C2", "SPI1", "SPI2", "USART1", "USART2",
    "RNG_LPUART1", "LCD", "USB",
]


def vector_symbols(data, base, prefix, table_addr):
    """Return {thumb_addr: name} for handlers in the table at table_addr."""
    names = CORE_VECTORS + [n and f"{n}_IRQHandler" for n in STM32L0_IRQS]
    off = table_addr - base
    syms, seen = {}, {}
    for i, name in enumerate(names):
        if name is None or i == 0:
            continue
        (vec,) = struct.unpack_from("<I", data, off + 4 * i)
        if not (base <= vec < base + len(data)) or not vec & 1:
            continue
        addr = vec & ~1
        # Many vectors share one default handler; keep the first, alias the rest.
        if addr in seen:
            syms[addr] = f"{prefix}_Default_Handler"
            continue
        seen[addr] = name
        syms[addr] = f"{prefix}_{name}"
    return syms


def build_elf(data, base, entry, symbols):
    shstr = b"\0.text\0.symtab\0.strtab\0.shstrtab\0"
    strtab = b"\0"
    syms = [b"\0" * 16]  # null symbol
    for addr, name in sorted(symbols.items()):
        name_off = len(strtab)
        strtab += name.encode() + b"\0"
        # st_name, st_value(thumb bit set), st_size, st_info(GLOBAL FUNC), st_other, st_shndx=1
        syms.append(struct.pack("<IIIBBH", name_off, addr | 1, 0, 0x12, 0, 1))
    symtab = b"".join(syms)

    ehdr_size, phdr_size, shdr_size = 52, 32, 40
    text_off = 0x1000
    symtab_off = text_off + len(data)
    strtab_off = symtab_off + len(symtab)
    shstr_off = strtab_off + len(strtab)
    shoff = (shstr_off + len(shstr) + 3) & ~3

    ehdr = struct.pack(
        "<16sHHIIIIIHHHHHH",
        b"\x7fELF\x01\x01\x01" + b"\0" * 9,
        2, 40, 1,                 # ET_EXEC, EM_ARM, EV_CURRENT
        entry, ehdr_size, shoff,
        0x05000200,               # EABI v5, soft-float
        ehdr_size, phdr_size, 1, shdr_size, 5, 4,
    )
    phdr = struct.pack("<IIIIIIII", 1, text_off, base, base, len(data), len(data), 5, 0x1000)

    def sh(name, typ, flags, addr, off, size, link=0, info=0, align=1, entsize=0):
        return struct.pack("<IIIIIIIIII", name, typ, flags, addr, off, size, link, info, align, entsize)

    shdrs = b"".join([
        sh(0, 0, 0, 0, 0, 0),
        sh(shstr.index(b".text"), 1, 0x6, base, text_off, len(data), align=4),
        sh(shstr.index(b".symtab"), 2, 0, 0, symtab_off, len(symtab), link=3, info=1, align=4, entsize=16),
        sh(shstr.index(b".strtab"), 3, 0, 0, strtab_off, len(strtab)),
        sh(shstr.index(b".shstrtab"), 3, 0, 0, shstr_off, len(shstr)),
    ])

    out = bytearray(ehdr + phdr)
    out += b"\0" * (text_off - len(out))
    out += data + symtab + strtab + shstr
    out += b"\0" * (shoff - len(out))
    out += shdrs
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x08000000)
    ap.add_argument("--table", action="append", default=[],
                    help="PREFIX:ADDR of a vector table, e.g. app:0x08001000")
    ap.add_argument("--entry", type=lambda s: int(s, 0),
                    help="entry address (default: reset vector of the first table)")
    args = ap.parse_args()

    data = open(args.input, "rb").read()
    # Trim trailing erased flash (STM32L0 erases to 0x00).
    end = len(data)
    while end and data[end - 1] == 0:
        end -= 1
    data = data[:(end + 3) & ~3]

    symbols = {}
    for spec in args.table:
        prefix, addr = spec.split(":")
        symbols.update(vector_symbols(data, args.base, prefix, int(addr, 0)))

    entry = args.entry
    if entry is None and args.table:
        addr = int(args.table[0].split(":")[1], 0)
        entry = struct.unpack_from("<I", data, addr - args.base + 4)[0]
    entry = entry or args.base

    open(args.output, "wb").write(build_elf(data, args.base, entry, symbols))
    print(f"wrote {args.output}: {len(data)} bytes at 0x{args.base:08X}, entry 0x{entry:08X}")
    for addr, name in sorted(symbols.items()):
        print(f"  0x{addr:08X} {name}")


if __name__ == "__main__":
    main()
