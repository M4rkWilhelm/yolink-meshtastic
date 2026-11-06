#!/usr/bin/env python3
"""Log packets a Meshtastic node (e.g. a Heltec on USB) receives from the sensor.

Usage: heltec_listen.py --node <sensor node id, hex> [--port /dev/ttyUSB0] [--minutes 15]
(The sensor's node id is in its setup-console banner, e.g. "YoLink Motion !1a2b3c4d", without the '!'.)
Prints one line per packet from that node: time, port, text/name, RSSI, SNR, hops.
"""
import argparse
import sys
import time

import meshtastic.serial_interface
from pubsub import pub


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--node", required=True, help="sender node id (hex, no '!')")
    ap.add_argument("--minutes", type=float, default=15)
    args = ap.parse_args()
    want = int(args.node, 16)

    def on_receive(packet, interface):
        if packet.get("from") != want:
            return
        d = packet.get("decoded", {})
        port = d.get("portnum", "ENCRYPTED/UNKNOWN")
        if "text" in d:
            what = repr(d["text"])
        elif port == "TELEMETRY_APP":
            t = d.get("telemetry", {})
            what = str(t.get("environmentMetrics") or t.get("deviceMetrics") or t)
        elif port == "NODEINFO_APP":
            u = d.get("user", {})
            what = f"user {u.get('id')} '{u.get('longName')}' ({u.get('shortName')}) role={u.get('role')}"
        else:
            what = ""
        hops = (packet.get("hopStart") or 0) - (packet.get("hopLimit") or 0)
        print(f"{time.strftime('%H:%M:%S')} {port:22s} {what}  rssi={packet.get('rxRssi')} "
              f"snr={packet.get('rxSnr')} hops={hops} ch={packet.get('channel')}", flush=True)

    pub.subscribe(on_receive, "meshtastic.receive")
    iface = meshtastic.serial_interface.SerialInterface(args.port, timeout=150)
    print(f"listening on {args.port} for !{args.node} ({args.minutes} min)", flush=True)
    try:
        time.sleep(args.minutes * 60)
    finally:
        iface.close()


if __name__ == "__main__":
    sys.exit(main())
