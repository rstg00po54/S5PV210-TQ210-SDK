#!/usr/bin/env python3
"""Read-only investigation of the vendor NAND boot image, not a flash packer."""
import hashlib
import pathlib
import struct

root = pathlib.Path(__file__).resolve().parents[2]
p = root / "factory-images/u-boot/uboot_TQ210_1.3.4_V1.7.bin"
data = p.read_bytes()
print("File:", p.name)
print("Length:", len(data), "bytes")
print("SHA256:", hashlib.sha256(data).hexdigest())
for off in (0, 0x10, 0x200, 0x800, 0x1000, 0x1ff0, 0x2000, 0x3ff0, 0x4000):
    chunk = data[off:off+16]
    if len(chunk) == 16:
        print("%06x:" % off, " ".join("%08x" % w for w in struct.unpack("<4I", chunk)))
for off in (0, 0x10):
    insn, = struct.unpack_from("<I", data, off)
    if (insn & 0xff000000) == 0xea000000:
        disp = insn & 0xffffff
        if disp & 0x800000:
            disp -= 0x1000000
        print("ARM B at file offset %#x -> file offset %#x" % (off, off+8+4*disp))
for size in (0x1000, 0x2000, 0x4000):
    if len(data) >= size:
        print("Candidate window %#x: sum(all)=%08x sum(after 16B)=%08x sum(before final word)=%08x final_word=%08x" %
              (size, sum(data[:size]) & 0xffffffff, sum(data[16:size]) & 0xffffffff,
               sum(data[:size-4]) & 0xffffffff, struct.unpack_from("<I", data, size-4)[0]))
print("Header fields and checksums above are observations, not a validated iROM format.")
