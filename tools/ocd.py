#!/usr/bin/env python3
"""Send commands to a running OpenOCD over its Tcl port and print the replies.

Usage: ocd.py "cmd1" ["cmd2" ...]
Exits 1 if OpenOCD isn't reachable or a reply contains an error.
"""
import socket
import sys


def main():
    try:
        s = socket.create_connection(("localhost", 6666), timeout=120)
    except OSError:
        print("OpenOCD is not running; start it with tools/connect.sh", file=sys.stderr)
        sys.exit(1)
    failed = False
    for c in sys.argv[1:]:
        s.sendall(f"capture {{{c}}}\x1a".encode())
        buf = b""
        while not buf.endswith(b"\x1a"):
            chunk = s.recv(4096)
            if not chunk:
                sys.exit("OpenOCD closed the connection")
            buf += chunk
        reply = buf[:-1].decode(errors="replace").strip()
        if reply:
            print(reply)
        if any(w in reply.lower() for w in ("error", "failed", "not halted")):
            failed = True
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
