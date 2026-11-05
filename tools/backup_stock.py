#!/usr/bin/env python3
"""Back up a YoLink sensor's original firmware and EEPROM before flashing.

Run tools/connect.sh first (reset jumper held, released when it says ARMED) so
the chip is halted before the stock firmware can disable SWD. This script only
reads: it never erases or writes the chip.

Each region is read twice and the copies compared. Output goes to
backup/<chip-uid>/ with a SHA256SUMS file.

The backup contains YoLink's copyrighted firmware and this sensor's own network
keys (in the EEPROM). Keep it private: don't commit or share it.

Usage: backup_stock.py [--out DIR]
"""
import argparse
import hashlib
import pathlib
import socket
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

REGIONS = [  # name, address, size
    ("flash", 0x08000000, 192 * 1024),
    ("eeprom", 0x08080000, 6 * 1024),
    ("optbytes", 0x1FF80000, 0x20),
]


class Ocd:
    def __init__(self):
        try:
            self.s = socket.create_connection(("localhost", 6666), timeout=120)
        except OSError:
            sys.exit("OpenOCD isn't running: start it with tools/connect.sh")

    def cmd(self, c):
        self.s.sendall((c + "\x1a").encode())
        b = b""
        while not b.endswith(b"\x1a"):
            b += self.s.recv(65536)
        return b[:-1].decode(errors="replace").strip()

    def word(self, addr):
        reply = self.cmd(f"read_memory 0x{addr:08X} 32 1")
        try:
            return int(reply, 0)
        except ValueError:
            sys.exit(f"can't read 0x{addr:08X} ({reply}). Is the chip halted? Redo tools/connect.sh "
                     "with the reset jumper.")

    def read(self, addr, size):
        data = bytearray()
        for off in range(0, size, 4096):
            n = min(4096, size - off)
            reply = self.cmd(f"read_memory 0x{addr + off:08X} 8 {n}")
            try:
                data += bytes(int(x, 0) for x in reply.split())
            except ValueError:
                sys.exit(f"read failed at 0x{addr + off:08X}: {reply}")
        return bytes(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=pathlib.Path, help="output directory (default backup/<uid>)")
    args = ap.parse_args()

    ocd = Ocd()
    ocd.cmd("halt")
    if ocd.cmd("stm32l0.cpu curstate") != "halted":
        sys.exit("couldn't halt the chip")

    idcode = ocd.word(0x40015800)
    if idcode & 0xFFF != 0x447:
        sys.exit(f"unexpected device ID 0x{idcode & 0xFFF:03X} (want 0x447, STM32L07x)")
    rdp = ocd.word(0x4002201C) & 0xFF  # FLASH_OPTR.RDPROT
    if rdp != 0xAA:
        sys.exit(f"readout protection is on (RDP=0x{rdp:02X}): the flash can't be read. Flashing would "
                 "erase the stock firmware with no backup possible. Stopping.")
    uid = "".join(f"{ocd.word(a):08x}" for a in (0x1FF80050, 0x1FF80054, 0x1FF80064))

    out = args.out or ROOT / "backup" / uid
    out.mkdir(parents=True, exist_ok=True)
    sums = []
    for name, addr, size in REGIONS:
        a = ocd.read(addr, size)
        b = ocd.read(addr, size)
        if a != b:
            sys.exit(f"{name}: two reads differ; check the wiring and try again")
        path = out / f"{name}.bin"
        path.write_bytes(a)
        digest = hashlib.sha256(a).hexdigest()
        sums.append(f"{digest}  {path.name}")
        print(f"{name:9s} {size:7d} bytes  sha256 {digest[:16]}...  (read twice, identical)")
    (out / "SHA256SUMS").write_text("\n".join(sums) + "\n")
    (out / "README.txt").write_text(
        f"Stock YoLink backup, chip UID {uid}, device ID 0x{idcode:08X}, RDP 0x{rdp:02X}.\n"
        "Contains YoLink's firmware and this sensor's network keys: keep private.\n"
        "Restore: tools/flash.sh <this dir>/flash.bin "
        "(the EEPROM is untouched by flashing; restore it only if you changed it).\n")
    print(f"backup written to {out}")


if __name__ == "__main__":
    main()
