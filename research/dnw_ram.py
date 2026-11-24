#!/usr/bin/env python3
"""TQ210 RAM download experiment, modeled on vendor ordinary USB path.
Requires libusb-1.0 runtime only. No flash commands. Hardware not yet verified.
"""
import argparse
import ctypes as C
import ctypes.util
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
BL2 = ROOT / "Linux平台工具/opt/EmbedSky/TQBoardDNW for linux/V210_USB.BL2.bin"

def chunks(data, addr, checksum):
    yield struct.pack("<II", addr, len(data) + 10) + data[:2040]
    for off in range(2040, len(data), 2048):
        yield data[off:off + 2048]
    yield struct.pack("<H", sum(data[:4096] if checksum == "vendor" else data) & 0xffff)

def selftest():
    data = bytes(range(256)) * 20
    a = list(chunks(data, 0xd0020010, "vendor"))
    assert [len(x) for x in a] == [2048, 2048, 1032, 2]
    assert a[0][:8] == bytes.fromhex("100002d00a140000")
    assert b"".join(a)[8:-2] == data
    assert a[-1] == struct.pack("<H", sum(data[:4096]) & 65535)
    assert list(chunks(data, 0xd0020010, "full"))[-1] == struct.pack("<H", sum(data) & 65535)
    assert [len(x) for x in chunks(b"x", 0x23e00000, "vendor")] == [9, 2]
    print("Packet tests passed")

class Endpoint(C.Structure):
    _fields_ = [("bLength", C.c_uint8), ("bDescriptorType", C.c_uint8),
                ("bEndpointAddress", C.c_uint8), ("bmAttributes", C.c_uint8),
                ("wMaxPacketSize", C.c_uint16), ("bInterval", C.c_uint8),
                ("bRefresh", C.c_uint8), ("bSynchAddress", C.c_uint8),
                ("extra", C.c_void_p), ("extra_length", C.c_int)]

class Alt(C.Structure):
    _fields_ = [(k, C.c_uint8) for k in
                ("bLength", "bDescriptorType", "bInterfaceNumber",
                 "bAlternateSetting", "bNumEndpoints", "bInterfaceClass",
                 "bInterfaceSubClass", "bInterfaceProtocol", "iInterface")] + [
                 ("endpoint", C.POINTER(Endpoint)), ("extra", C.c_void_p),
                 ("extra_length", C.c_int)]

class Interface(C.Structure):
    _fields_ = [("altsetting", C.POINTER(Alt)), ("num_altsetting", C.c_int)]

class Config(C.Structure):
    _fields_ = [("bLength", C.c_uint8), ("bDescriptorType", C.c_uint8),
                ("wTotalLength", C.c_uint16)] + [(k, C.c_uint8) for k in
                ("bNumInterfaces", "bConfigurationValue", "iConfiguration",
                 "bmAttributes", "MaxPower")] + [
                 ("interface", C.POINTER(Interface)), ("extra", C.c_void_p),
                 ("extra_length", C.c_int)]

def send(packets, vid, pid, timeout, reconfigure=True):
    lib = C.CDLL(C.util.find_library("usb-1.0") or "libusb-1.0.so.0")
    P, I = C.c_void_p, C.c_int
    def bind(name, restype, *args):
        f = getattr(lib, "libusb_" + name)
        f.restype, f.argtypes = restype, args
        return f
    init = bind("init", I, C.POINTER(P))
    exit_ = bind("exit", None, P)
    open_ = bind("open_device_with_vid_pid", P, P, C.c_uint16, C.c_uint16)
    close = bind("close", None, P)
    getdev = bind("get_device", P, P)
    setcfg = bind("set_configuration", I, P, I)
    getcurrent = bind("get_configuration", I, P, C.POINTER(I))
    getcfg = bind("get_active_config_descriptor", I, P, C.POINTER(C.POINTER(Config)))
    freecfg = bind("free_config_descriptor", None, C.POINTER(Config))
    claim = bind("claim_interface", I, P, I)
    release = bind("release_interface", I, P, I)
    bulk = bind("bulk_transfer", I, P, C.c_ubyte, P, I, C.POINTER(I), C.c_uint)
    err = bind("error_name", C.c_char_p, I)
    def check(rc, label):
        if rc < 0:
            raise RuntimeError(f"{label}: {err(rc).decode()}")
    ctx = P()
    check(init(C.byref(ctx)), "init")
    h = None
    interface = None
    try:
        h = open_(ctx, vid, pid)
        if not h:
            raise RuntimeError(f"{vid:04x}:{pid:04x} not accessible")
        current = I()
        check(getcurrent(h, C.byref(current)), "get configuration")
        print(f"Current configuration: {current.value}", flush=True)
        if reconfigure:
            check(setcfg(h, 1), "SET_CONFIGURATION(1)")
            print("SET_CONFIGURATION(1): OK", flush=True)
        cfg = C.POINTER(Config)()
        check(getcfg(getdev(h), C.byref(cfg)), "get config")
        try:
            eps = []
            for i in range(cfg.contents.bNumInterfaces):
                it = cfg.contents.interface[i]
                for j in range(it.num_altsetting):
                    alt = it.altsetting[j]
                    if alt.bAlternateSetting != 0 or alt.bInterfaceClass != 255:
                        continue
                    for k in range(alt.bNumEndpoints):
                        ep = alt.endpoint[k]
                        if ep.bmAttributes & 3 == 2 and not ep.bEndpointAddress & 128:
                            eps.append((alt.bInterfaceNumber, ep.bEndpointAddress,
                                        ep.wMaxPacketSize & 2047))
            if len(eps) != 1:
                raise RuntimeError(f"Expected one vendor bulk OUT endpoint, found {eps}")
            iface, ep, mps = eps[0]
        finally:
            freecfg(cfg)
        check(claim(h, iface), "claim interface")
        interface = iface
        print(f"USB {vid:04x}:{pid:04x}, interface {iface}, OUT 0x{ep:02x}, MPS {mps}", flush=True)
        total = 0
        for i, packet in enumerate(packets):
            buf = C.create_string_buffer(packet)
            sent = I()
            rc = bulk(h, ep, C.cast(buf, P), len(packet), C.byref(sent), timeout)
            print(f"Transfer {i}: {sent.value}/{len(packet)} bytes, {err(rc).decode()}", flush=True)
            check(rc, "bulk")
            if sent.value != len(packet):
                raise RuntimeError("Short write. Reset target before retrying.")
            total += sent.value
        print(f"USB transport complete: {total} bytes. CPU execution NOT confirmed.")
    finally:
        if h:
            if interface is not None:
                release(h, interface)
            close(h)
        exit_(ctx)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("stage", nargs="?", choices=["stage1", "stage2"])
    ap.add_argument("file", nargs="?", type=pathlib.Path)
    ap.add_argument("--checksum", choices=["vendor", "full"], default="vendor")
    ap.add_argument("--vid", type=lambda s:int(s,16), default=0x04e8)
    ap.add_argument("--pid", type=lambda s:int(s,16), default=0x1234)
    ap.add_argument("--timeout", type=int, default=2000)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--join-tail", action="store_true",
                    help="append checksum to last data transfer (experimental)")
    ap.add_argument("--no-reconfigure", action="store_true",
                    help="skip explicit SET_CONFIGURATION for A/B comparison")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return
    if not args.stage:
        ap.error("choose stage1 or stage2")
    if args.timeout < 1 or not 0 <= args.vid <= 65535 or not 0 <= args.pid <= 65535:
        ap.error("invalid USB arguments")
    if args.stage == "stage1":
        if args.file:
            ap.error("stage1 uses the bundled vendor BL2 only")
        path, addr, limit = BL2, 0xd0020010, 16384-16
    else:
        if not args.file:
            ap.error("stage2 requires a RAM-linked test binary")
        path, addr, limit = args.file, 0x23e00000, 65536
    data = path.read_bytes()
    if not 0 < len(data) <= limit:
        ap.error("image size outside experimental RAM limits")
    packets = list(chunks(data, addr, args.checksum))
    if args.join_tail:
        packets[-2:] = [packets[-2] + packets[-1]]
    print(f"{args.stage}: {path.name}, address 0x{addr:08x}, payload {len(data)}, "
          f"wire {len(data)+10}, {args.checksum} checksum 0x{int.from_bytes(packets[-1][-2:], 'little'):04x}")
    print("Header:", packets[0][:8].hex(" "), "Transfer sizes:", [len(p) for p in packets])
    if not args.dry_run:
        send(packets, args.vid, args.pid, args.timeout, not args.no_reconfigure)

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
