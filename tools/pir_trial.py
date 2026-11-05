#!/usr/bin/env python3
"""Drive the PIR sensitivity test firmware (firmware-tests/pir_sensitivity) over SWD.

  pir_trial.py start <select>   set PC2/PC3 (0..3: bit1 = PC2, bit0 = PC3) and clear stats
  pir_trial.py read             print the stats since the last start

Needs OpenOCD from tools/connect.sh.
"""
import pathlib
import socket
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ELF = ROOT / "firmware-tests/pir_sensitivity/.pio/build/pir_sensitivity/firmware.elf"
NM = pathlib.Path.home() / ".platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm"
FIELDS = ["magic", "select", "reset", "applied", "samples", "edges", "minRaw", "maxRaw",
          "baselineX16", "maxDev", "sumDev", "pir"]


def addr():
    out = subprocess.run([str(NM), str(ELF)], capture_output=True, text=True, check=True).stdout
    return next(int(l.split()[0], 16) for l in out.splitlines() if l.endswith(" g_test"))


def main():
    s = socket.create_connection(("localhost", 6666), timeout=20)

    def cmd(c):
        s.sendall((c + "\x1a").encode())
        b = b""
        while not b.endswith(b"\x1a"):
            b += s.recv(4096)
        return b[:-1].decode(errors="replace").strip()

    a = addr()
    if len(sys.argv) >= 3 and sys.argv[1] == "start":
        sel = int(sys.argv[2]) & 3
        cmd(f"write_memory 0x{a + 4:08X} 32 {{{sel}}}")   # select
        cmd(f"write_memory 0x{a + 8:08X} 32 {{1}}")       # reset
        print(f"selected {sel}: PC2={sel >> 1 & 1} PC3={sel & 1}; stats cleared")
    elif len(sys.argv) >= 2 and sys.argv[1] == "read":
        vals = [int(x, 0) for x in cmd(f"read_memory 0x{a:08X} 32 {len(FIELDS)}").split()]
        d = dict(zip(FIELDS, vals))
        if d["magic"] != 0x54524950:
            sys.exit("test firmware not running (magic mismatch)")
        avg = d["sumDev"] / d["samples"] if d["samples"] else 0
        print(f"PC2={d['applied'] >> 1 & 1} PC3={d['applied'] & 1}  samples={d['samples']}  "
              f"edges={d['edges']}  raw {d['minRaw']}..{d['maxRaw']}  "
              f"rest={d['baselineX16'] / 16:.1f}  maxDev={d['maxDev']}  avgDev={avg:.2f}")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
