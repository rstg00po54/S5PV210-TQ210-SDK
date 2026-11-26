#!/usr/bin/env python3
"""Offline TQ210 BL2 slot image: header page then padded U-Boot payload."""
import argparse
from pathlib import Path
import struct
import zlib

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("uboot", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
data = a.uboot.read_bytes()
if not 0 < len(data) <= 0x100000 or len(data) % 4:
    p.error("U-Boot must be 4-byte aligned and <= 1 MiB")
header = struct.pack("<4I", 0x324c4254, len(data), zlib.crc32(data), 0x24000000)
header += struct.pack("<I", zlib.crc32(header))
image = header.ljust(2048, b"\xff") + data.ljust((len(data)+2047)//2048*2048, b"\xff")
with a.output.open("xb") as f:
    f.write(image)
print("BL2 payload=%d CRC32=%08x slot image=%d" % (len(data), zlib.crc32(data), len(image)))
print("OFFLINE ONLY; NAND slot starts at 0x00100000")
