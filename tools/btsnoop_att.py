#!/usr/bin/env python3
"""Read an Android Bluetooth HCI snoop log (btsnoop_hci.log) and print the GATT traffic.

Standard library only (works with the Python 3.9 that ships with macOS).

    python3 tools/btsnoop_att.py btsnoop_hci.log

Prints, in time order:
  WRITE  app -> module   (write request / write command)
  NOTIFY module -> app   (notification / indication)
  READ   module -> app   (read response)
with the characteristic UUID when the service discovery was captured.
Filter noise with --uuid ffe (substring match on the UUID).
"""
import argparse
import struct
import sys

OPCODES = {
    0x12: "WRITE", 0x52: "WRITE-CMD", 0x1B: "NOTIFY", 0x1D: "INDICATE",
    0x0A: "READ-REQ", 0x0B: "READ-RSP", 0x13: "WRITE-RSP",
}


def records(data):
    if data[:8] != b"btsnoop\x00":
        sys.exit("Not a btsnoop file (missing 'btsnoop' header).")
    pos = 16
    while pos + 24 <= len(data):
        orig, incl, flags, drops, ts = struct.unpack(">IIIIq", data[pos:pos + 24])
        pos += 24
        yield flags, ts, data[pos:pos + incl]
        pos += incl


def uuid_str(b):
    if len(b) == 2:
        return "%04x" % struct.unpack("<H", b)[0]
    return b[::-1].hex()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--uuid", help="only show characteristics whose UUID contains this text")
    args = ap.parse_args()
    data = open(args.file, "rb").read()

    handle_uuid = {}
    partial = {}  # connection handle -> (expected_len, bytearray)
    t0 = None
    out = []
    for flags, ts, pkt in records(data):
        if not pkt or pkt[0] != 0x02:  # HCI ACL data only
            continue
        sent = (flags & 1) == 0  # 0 = host to controller (phone sent it)
        hdr, alen = struct.unpack("<HH", pkt[1:5])
        conn, pb = hdr & 0x0FFF, (hdr >> 12) & 0x3
        payload = pkt[5:5 + alen]
        if pb in (0, 2):  # start of an L2CAP PDU
            if len(payload) < 4:
                continue
            l2len, cid = struct.unpack("<HH", payload[:4])
            body = bytearray(payload[4:])
            if len(body) < l2len:
                partial[conn] = (l2len, cid, body)
                continue
        else:  # continuation fragment
            if conn not in partial:
                continue
            l2len, cid, body = partial[conn]
            body.extend(payload)
            if len(body) < l2len:
                continue
            del partial[conn]
        if cid != 0x0004 or not body:  # ATT only
            continue
        op, rest = body[0], bytes(body[1:])

        # Learn handle -> UUID from characteristic discovery responses
        if op == 0x09 and rest:  # Read By Type Response
            item = rest[0]
            for i in range(1, len(rest) - item + 1, item):
                e = rest[i:i + item]
                if item in (7, 21):
                    value_handle = struct.unpack("<H", e[3:5])[0]
                    handle_uuid[value_handle] = uuid_str(e[5:])
            continue

        if op not in OPCODES or op in (0x13,) or len(rest) < 2:
            continue
        if op == 0x0B:  # read response has no handle
            h, val = None, rest
        else:
            h = struct.unpack("<H", rest[:2])[0]
            val = rest[2:]
        name = handle_uuid.get(h, "?" if h is not None else "-")
        if args.uuid and args.uuid.lower() not in name:
            continue
        if t0 is None:
            t0 = ts
        secs = (ts - t0) / 1e6
        arrow = "app->module" if sent else "module->app"
        txt = "".join(chr(c) if 32 <= c < 127 else ("|" if c == 13 else ".") for c in val[:60])
        out.append(f"{secs:9.3f}s {OPCODES[op]:9s} {arrow} handle=0x{h:04x} uuid={name:>9s} len={len(val):3d} "
                   f"{val[:48].hex(' ')}  {txt}" if h is not None else
                   f"{secs:9.3f}s {OPCODES[op]:9s} {arrow} len={len(val):3d} {val[:48].hex(' ')}  {txt}")
    print("\n".join(out) if out else "No ATT read/write/notify traffic found in this file.")


if __name__ == "__main__":
    main()
