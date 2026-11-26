#!/usr/bin/env python3
"""Offline S5PV210 EVT1 BL1 image packer. No device access, no OOB."""
import argparse
import hashlib
from pathlib import Path
import struct

SIZE = 8192
HEADER = 16

def pack(payload):
    if len(payload) < 4 or len(payload) > SIZE - HEADER or len(payload) % 4:
        raise ValueError("payload must be ARM word aligned, 4..8176 bytes")
    insn, = struct.unpack_from("<I", payload)
    if insn & 0xff000000 != 0xea000000:
        raise ValueError("expected unconditional ARM B at payload entry")
    disp = insn & 0xffffff
    if disp & 0x800000:
        disp -= 0x1000000
    dest = 8 + 4 * disp
    if not 0 <= dest < len(payload):
        raise ValueError("entry branch outside payload")
    body = payload.ljust(SIZE - HEADER, b"\0")
    return struct.pack("<4I", SIZE, 0, sum(body), 0) + body

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", type=Path, help="headerless binary linked at 0xd0020010")
    parser.add_argument("output", type=Path, help="new output file; never overwrite")
    args = parser.parse_args()
    payload = args.payload.read_bytes()
    try:
        image = pack(payload)
    except ValueError as exc:
        parser.error(str(exc))
    with args.output.open("xb") as stream:
        stream.write(image)
    length, _, checksum, _ = struct.unpack_from("<4I", image)
    assert length == len(image) == SIZE
    assert checksum == sum(image[HEADER:])
    print("Payload: %d bytes; BL1: %d bytes; checksum: 0x%08x" %
          (len(payload), length, checksum))
    print("SHA256:", hashlib.sha256(image).hexdigest())
    print("OFFLINE PACK PASS; no NAND/OOB operations performed")

if __name__ == "__main__":
    main()
