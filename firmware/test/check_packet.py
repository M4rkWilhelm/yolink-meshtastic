#!/usr/bin/env python3
"""Decode packets printed by packet_test.cpp with an independent implementation
of the Meshtastic on-air format (pycryptodome AES-CTR + a hand-rolled protobuf
reader) and check every field. Exits non-zero on any mismatch.
"""
import struct
import sys

from Crypto.Cipher import AES
from Crypto.Util import Counter

DEFAULT_PSK = bytes.fromhex("d4f1bb3a20290759f0bcffabcf4e6901")
KEY256 = bytes(range(32))  # matches key256 in packet_test.cpp
FROM = 0x1234ABCD


def decrypt(key, packet_id, from_node, ciphertext):
    nonce = struct.pack("<QI", packet_id, from_node) + b"\0\0\0\0"
    ctr = Counter.new(32, prefix=nonce[:12], initial_value=int.from_bytes(nonce[12:], "big"))
    return AES.new(key, AES.MODE_CTR, counter=ctr).decrypt(ciphertext)


def xor_hash(name, key):
    h = 0
    for b in name + key:
        h ^= b
    return h


def parse_proto(buf):
    """Return {field: [values]}: varint -> int, length-delimited -> bytes,
    32-bit -> 4 raw bytes."""
    fields, i = {}, 0

    def varint():
        nonlocal i
        shift = value = 0
        while True:
            b = buf[i]
            i += 1
            value |= (b & 0x7F) << shift
            shift += 7
            if not b & 0x80:
                return value

    while i < len(buf):
        key = varint()
        field, wire = key >> 3, key & 7
        if wire == 0:
            value = varint()
        elif wire == 2:
            n = varint()
            value = buf[i:i + n]
            i += n
        elif wire == 5:
            value = buf[i:i + 4]
            i += 4
        else:
            raise ValueError(f"unexpected wire type {wire}")
        fields.setdefault(field, []).append(value)
    return fields


def check(cond, msg):
    if not cond:
        print("FAIL:", msg)
        sys.exit(1)


def main():
    check(xor_hash(b"LongFast", DEFAULT_PSK) == 8, "LongFast channel hash should be 8")

    for line in sys.stdin:
        label, hexdata = line.split()
        pkt = bytes.fromhex(hexdata)
        private = label in ("aes256", "detect", "tel_env", "tel_dev")
        key, name, hops = (KEY256, b"YLtest", 0) if private else (DEFAULT_PSK, b"LongFast", 3)
        to, frm, pid, flags, chan, next_hop, relay = struct.unpack_from("<IIIBBBB", pkt)
        check(to == 0xFFFFFFFF, f"{label}: to")
        check(frm == FROM, f"{label}: from")
        check(flags & 7 == hops and flags >> 5 == hops and not flags & 0x18,
              f"{label}: flags {flags:#x}")
        check(chan == xor_hash(name, key), f"{label}: channel hash {chan}")
        check(next_hop == 0 and relay == FROM & 0xFF, f"{label}: next_hop/relay")

        data = parse_proto(decrypt(key, pid, frm, pkt[16:]))
        port = data[1][0]
        payload = data[2][0]
        if label in ("text", "long", "aes256"):
            check(port == 1, f"{label}: portnum {port}")
            print(f"{label}: id={pid:#010x} len={len(pkt)} text={payload.decode()!r}")
        elif label == "detect":
            check(port == 10, f"{label}: portnum {port} (want DETECTION_SENSOR_APP = 10)")
            check(payload == b"Driveway detected\x07", f"{label}: payload {payload!r}")
            print(f"{label}: id={pid:#010x} len={len(pkt)} port=10 text={payload!r}")
        elif label == "tel_env":
            check(port == 67, f"{label}: portnum {port} (want TELEMETRY_APP = 67)")
            tel = parse_proto(payload)
            check(1 not in tel, "Telemetry.time should be omitted")
            env = parse_proto(tel[3][0])  # environment_metrics
            temp = struct.unpack("<f", env[1][0])[0]
            check(abs(temp - 23.0) < 1e-6, f"temperature {temp}")
            print(f"{label}: port=67 environment_metrics.temperature={temp}")
        elif label == "tel_dev":
            check(port == 67, f"{label}: portnum {port}")
            dev = parse_proto(parse_proto(payload)[2][0])  # device_metrics
            volts = struct.unpack("<f", dev[2][0])[0]
            check(dev[1][0] == 87 and abs(volts - 2.87) < 1e-6 and dev[5][0] == 3600,
                  f"device metrics {dev}")
            print(f"{label}: port=67 device_metrics battery={dev[1][0]}% voltage={volts:.2f} "
                  f"uptime={dev[5][0]}s")
        elif label == "nodeinfo":
            check(port == 4, f"{label}: portnum {port}")
            user = parse_proto(payload)
            check(user[1][0] == b"!1234abcd", "User.id")
            check(user[3][0] == b"abcd", "User.short_name")
            check(user[5][0] == 255 and user[7][0] == 6, "User.hw_model / role")
            print(f"nodeinfo: id={pid:#010x} len={len(pkt)} id={user[1][0].decode()} "
                  f"long_name={user[2][0].decode()!r} short_name={user[3][0].decode()!r}")
    print("all packets decoded and verified")


if __name__ == "__main__":
    main()
