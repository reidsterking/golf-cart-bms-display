#!/usr/bin/env python3
"""Scan for BLE devices from the Mac and print their advertising details.

Used to compare what the real GC2RB module advertises with what the board's
clone advertises. Run:  python3 tools/mac_scan.py [seconds]
Needs:  python3 -m pip install --user bleak
"""
import asyncio
import sys

from bleak import BleakScanner


def is_module(name, uuids):
    n = (name or "").lower()
    return "gc2rb" in n or any("ffe0" in u.lower() for u in uuids)


async def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    print(f"Scanning {secs:.0f}s ...")
    found = await BleakScanner.discover(timeout=secs, return_adv=True)
    rows = []
    for addr, (dev, adv) in found.items():
        mfg = " ".join(f"{cid:04X}:{bytes(v).hex()}" for cid, v in adv.manufacturer_data.items())
        rows.append((adv.rssi, adv.local_name, addr, list(adv.service_uuids), mfg))
    rows.sort(key=lambda r: -r[0])
    hits = [r for r in rows if is_module(r[1], r[3])]
    print(f"\n{len(rows)} devices seen, {len(hits)} look like the module:\n")
    for rssi, name, addr, uuids, mfg in hits:
        print(f"  <<< MODULE rssi={rssi} name={name!r} addr={addr}")
        print(f"      services={uuids} mfg={mfg}")
    print("\nStrongest 12 other devices:")
    for rssi, name, addr, uuids, mfg in [r for r in rows if r not in hits][:12]:
        print(f"  rssi={rssi} name={name!r} services={uuids} mfg={mfg[:40]}")


asyncio.run(main())
