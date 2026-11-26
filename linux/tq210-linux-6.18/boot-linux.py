#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Cold RAM boot through OpenBoot, U-Boot Fastboot and UART0."""
import argparse
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

import serial

SDK = pathlib.Path(__file__).resolve().parents[2]
LINUX = SDK / "linux"
OPENBOOT = SDK / "openboot"
PROMPT = b"TQ210 => "
STAGING = 0x26000000
SERIAL = "TQ210-TEST"

def run(args, **kw):
    return subprocess.run([str(a) for a in args], check=True, **kw)

def wait_for(description, predicate, timeout=60):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(0.35)
    raise RuntimeError("Timeout: " + description)

def usb_rom():
    p = subprocess.run(["lsusb", "-d", "04e8:1234"],
                       stdout=subprocess.PIPE, text=True, timeout=5)
    lines = [s for s in p.stdout.splitlines() if s.strip()]
    if len(lines) > 1:
        raise RuntimeError("Multiple ROM devices; disconnect other boards")
    return len(lines) == 1

def fastboot_ready():
    p = subprocess.run(["fastboot", "devices"], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, text=True, timeout=5)
    matches = [s for s in p.stdout.splitlines()
               if s.split()[:2] == [SERIAL, "fastboot"]]
    if len(matches) > 1:
        raise RuntimeError("Multiple TQ210-TEST devices")
    return len(matches) == 1

class Console:
    def __init__(self, port):
        self.port = port

    def send(self, data):
        self.port.write(data)
        self.port.flush()

    def read(self):
        data = self.port.read(self.port.in_waiting or 1)
        if data:
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
        return data

    def until(self, marker, timeout=30):
        end = time.monotonic() + timeout
        buf = b""
        while time.monotonic() < end:
            buf += self.read()
            if marker in buf:
                return buf
            if len(buf) > 262144:
                buf = buf[-131072:]
        raise RuntimeError("UART timeout waiting for " + repr(marker))

    def cmd(self, command):
        self.send(command.encode("ascii") + b"\r")
        result = self.until(PROMPT)
        if re.search(rb"Unknown command|ERROR:|Bad Magic|Bad magic|## Error", result):
            raise RuntimeError("U-Boot command failed: " + command)
        return result

    def verify(self, addr, data):
        expected = zlib.crc32(data) & 0xffffffff
        response = self.cmd("crc32 %x %x" % (addr, len(data)))
        found = re.search(rb"==>\s*([0-9a-fA-F]{8})", response)
        if not found or int(found.group(1), 16) != expected:
            raise RuntimeError("CRC mismatch at 0x%08x (expected %08x)" %
                               (addr, expected))
        print("CRC PASS: 0x%08x (%08x)" % (addr, expected), flush=True)

    def stage(self, path, data, dest):
        self.send(b"fastboot usb 0\r")
        wait_for("Fastboot TQ210-TEST enumeration", fastboot_ready)
        run(["fastboot", "-s", SERIAL, "stage", path], timeout=120)
        # USB command succeeded; now leave the polling loop over UART.
        self.send(b"\x03")
        self.until(PROMPT)
        self.verify(STAGING, data)
        if dest != STAGING:
            self.cmd("cp.b %x %x %x" % (STAGING, dest, len(data)))
            self.verify(dest, data)

def load(path, limit, kind):
    data = path.read_bytes()
    if not 0 < len(data) <= limit:
        raise RuntimeError("Unexpected image size: " + str(path))
    if kind == "dtb" and (len(data) < 40 or data[:4] != b"\xd0\x0d\xfe\xed"
                         or struct.unpack_from(">I", data, 4)[0] != len(data)):
        raise RuntimeError("Invalid DTB header/size")
    if kind == "ramdisk" and (len(data) < 64 or data[:4] != b"\x27\x05\x19\x56"
                             or struct.unpack_from(">I", data, 12)[0] != len(data)-64):
        raise RuntimeError("Invalid legacy ramdisk header/size")
    if kind == "kernel" and (len(data) < 48 or data[36:40] != b"\x18\x28\x6f\x01"):
        raise RuntimeError("Invalid ARM zImage magic")
    return data

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("port", help="UART0 adapter, e.g. /dev/ttyUSB0")
    ap.add_argument("--basic", action="store_true", help="boot known-good basic kernel")
    ap.add_argument("--uboot-ready", action="store_true",
                    help="skip ROM download; board must already be at TQ210 prompt")
    args = ap.parse_args()
    for tool in ("lsusb", "fastboot"):
        if not shutil.which(tool):
            raise RuntimeError("Missing host tool: " + tool)
    out = LINUX / ("build-tq210-6.18" if args.basic else "build-tq210-adb")
    boot = out / "arch/arm/boot"
    dtb = boot / "dts/samsung" / ("s5pv210-tq210.dtb" if args.basic else "s5pv210-tq210-adb.dtb")
    ramdisk = LINUX / "build-rootfs-tq210/rootfs.uimg"
    kernel = boot / "zImage"
    images = [(dtb, load(dtb, 0x100000, "dtb"), 0x2a000000),
              (ramdisk, load(ramdisk, 0x1000000, "ramdisk"), 0x2c000000),
              (kernel, load(kernel, 0x4000000, "kernel"), STAGING)]
    dnw = OPENBOOT / "build-host/tq210-dnw"
    loader = OPENBOOT / "out/rom_usb_dma_fast.bin"
    uboot = SDK / "u-boot/mainline/build-tq210/u-boot.bin"
    if not args.uboot_ready:
        for path in (dnw, loader, uboot):
            if not path.is_file() or not path.stat().st_size:
                raise RuntimeError("Missing boot artifact: " + str(path))
        if uboot.stat().st_size % 4 or uboot.stat().st_size > 0x1000000:
            raise RuntimeError("U-Boot size must be aligned to 4 bytes and <=16 MiB")
    with serial.Serial(args.port, 115200, timeout=0.15, write_timeout=3,
                       xonxoff=False, rtscts=False, dsrdtr=False,
                       exclusive=True) as port:
        c = Console(port)
        port.reset_input_buffer()
        if not args.uboot_ready:
            print("Power-cycle TQ210 into iROM USB download mode now.", flush=True)
            wait_for("ROM 04e8:1234", usb_rom, 120)
            run([dnw, "--dma-boot", loader, uboot], cwd=OPENBOOT, timeout=120)
        else:
            c.send(b"\r")
        c.until(PROMPT, 45)
        for path, data, dest in images:
            print("\n>>> Download " + str(path), flush=True)
            c.stage(path, data, dest)
        c.cmd("fdt addr 2a000000")
        c.cmd("fdt header")
        c.cmd("setenv bootargs 'console=ttySAC0,115200n8 earlycon ignore_loglevel clk_ignore_unused rdinit=/init'")
        c.send(b"bootz 26000000 2c000000 2a000000\r")
        c.until(b"BusyBox userspace ready", 90)
        print("\nRAM boot reached userspace. Releasing UART; reopen your terminal.", flush=True)

if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError, serial.SerialException) as exc:
        print("\nBOOT FAILED: " + str(exc), file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nInterrupted; board state may require a cold reset.", file=sys.stderr)
        sys.exit(130)
