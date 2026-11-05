#!/usr/bin/env python3
"""Print the motion firmware's temperature, gain level and counters every few
seconds over SWD (halts the core briefly for each read).

Usage: watch_status.py [--seconds 5] [--minutes 10]
"""
import argparse
import pathlib
import socket
import struct
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
ELF = ROOT / "firmware/.pio/build/ys7805_debug/firmware.elf"
NM = pathlib.Path.home() / ".platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm"
GAIN = ["low", "medium", "high", "max"]


def reset_cause(csr):
    # RCC_CSR reset flags recorded at boot (latest reset; several can be set).
    names = {31: "lowpower", 30: "wwdg", 29: "WATCHDOG", 28: "software", 27: "power-on",
             26: "pin", 25: "brownout", 24: "fw"}
    return "+".join(n for b, n in names.items() if csr >> b & 1) or hex(csr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=5)
    ap.add_argument("--minutes", type=float, default=10)
    args = ap.parse_args()
    out = subprocess.run([str(NM), str(ELF)], capture_output=True, text=True, check=True).stdout
    addr = next(int(l.split()[0], 16) for l in out.splitlines() if l.endswith(" g_status"))
    s = socket.create_connection(("localhost", 6666), timeout=20)

    def cmd(c):
        s.sendall((c + "\x1a").encode())
        b = b""
        while not b.endswith(b"\x1a"):
            b += s.recv(4096)
        return b[:-1].decode(errors="replace").strip()

    end = time.time() + args.minutes * 60
    while time.time() < end:
        cmd("halt")
        v = [int(x, 0) for x in cmd(f"read_memory 0x{addr:08X} 32 18").split()]
        cmd("resume")
        temp = struct.unpack("<i", struct.pack("<I", v[9]))[0]
        gain = GAIN[v[11]] if v[11] < 4 else str(v[11])
        print(f"{time.strftime('%H:%M:%S')}  temp={temp:3d} C  gain={gain:6s}  vdd={v[10]} mV  "
              f"edges={v[4]} alerts={v[5]} sent={v[7]} wakeups={v[8]} reset={reset_cause(v[12])}"
              + (f" faults={v[13]} last=exc{v[15]}@0x{v[16]:08x} lr=0x{v[17]:08x}" if v[13] else "")
              + (f" wdg_resets={v[14]}" if v[14] else ""),
              flush=True)
        time.sleep(args.seconds)


if __name__ == "__main__":
    main()
