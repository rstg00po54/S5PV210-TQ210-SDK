#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Narrow USB-only port of the pinned KitKat-derived adbd for a private lab."""
import pathlib
import sys

src = pathlib.Path(sys.argv[1])
p = src / "adb.c"
s = p.read_text()
# The upstream Linux port comments out property_get but reads its buffers.
old = 'char value[PROPERTY_VALUE_MAX];'
if s.count(old) < 3:
    raise SystemExit("Unexpected adb.c revision")
s = s.replace(old, 'char value[PROPERTY_VALUE_MAX] = {0};')
# Don't expose a host-protocol listener or default unauthenticated TCP adbd.
start = s.index("    /* don't listen on a port (default 5037)")
end = s.index("    //D(\"adb_main(): pre init_jdwp()", start)
s = s[:start] + '''    /* TQ210: only USB FunctionFS; no network listener. */
    if (access(USB_FFS_ADB_EP0, F_OK) != 0) {
        fprintf(stderr, "TQ210 adbd: FunctionFS ep0 missing\n");
        return 1;
    }
    usb_init();

''' + s[end:]
# Python triple string above needs a literal C escape, not a newline.
s = s.replace('FunctionFS ep0 missing\n");', 'FunctionFS ep0 missing\\n");')
s = s.replace('    start_device_log();', '    /* Use inherited stdout/stderr. */')
p.write_text(s)
p = src / "usb_linux_client.c"
s = p.read_text()
old = '    h->bulk_out = -1;\n    h->bulk_out = -1;'
if old not in s:
    raise SystemExit("Unexpected USB source revision")
s = s.replace(old, '    h->bulk_out = -1;\n    h->bulk_in = -1;')


# glibc htole16/32 may expand to inline functions, which cannot initialize
# static descriptors. This port targets little-endian ARM only.
old = '#define cpu_to_le16(x)  htole16(x)\n#define cpu_to_le32(x)  htole32(x)'
new = '''#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "TQ210 adbd requires a little-endian target"
#endif
#define cpu_to_le16(x) ((__le16)(x))
#define cpu_to_le32(x) ((__le32)(x))'''
if old not in s:
    raise SystemExit("Unexpected endian macro definitions")
s = s.replace(old, new)
p.write_text(s)
