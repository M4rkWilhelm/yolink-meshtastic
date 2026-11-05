#!/usr/bin/env python3
"""Print the firmware's RAM log (src/ramlog.cpp) over SWD.

Usage: ramlog.py [path/to/firmware.elf]   (default: the ys7805_debug build)
Needs OpenOCD from tools/connect.sh. The CPU is halted briefly while reading.
"""
import pathlib
import socket
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
NM = pathlib.Path.home() / ".platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm"


def symbols(elf):
    out = subprocess.run([str(NM), "-S", str(elf)], capture_output=True, text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4:
            syms[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
    return syms


def main():
    elf = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else \
        ROOT / "firmware/.pio/build/ys7805_debug/firmware.elf"
    syms = symbols(elf)
    buf_addr, buf_size = syms["g_ramLogBuf"]
    total_addr, _ = syms["g_ramLogTotal"]

    s = socket.create_connection(("localhost", 6666), timeout=30)

    def cmd(c):
        s.sendall((c + "\x1a").encode())
        b = b""
        while not b.endswith(b"\x1a"):
            b += s.recv(65536)
        return b[:-1].decode(errors="replace").strip()

    cmd("halt")
    total = int(cmd(f"read_memory 0x{total_addr:08X} 32 1"), 0)
    raw = bytes(int(x, 0) for x in cmd(f"read_memory 0x{buf_addr:08X} 8 {buf_size}").split())
    cmd("resume")

    if total > buf_size:
        start = total % buf_size
        raw = raw[start:] + raw[:start]
        print(f"[... {total - buf_size} older bytes overwritten ...]")
    else:
        raw = raw[:total]
    sys.stdout.write(raw.decode(errors="replace"))


if __name__ == "__main__":
    main()
