#!/usr/bin/env python3
from pathlib import Path
import hashlib
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: verify_release_binary.py <exe>")

path = Path(sys.argv[1])
data = path.read_bytes()
if data[:2] != b"MZ":
    raise SystemExit("FAILED: not a PE executable")
pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
if data[pe_offset:pe_offset + 4] != b"PE\0\0":
    raise SystemExit("FAILED: invalid PE signature")
optional_offset = pe_offset + 4 + 20
subsystem = struct.unpack_from("<H", data, optional_offset + 68)[0]
if subsystem != 2:
    raise SystemExit(f"FAILED: PE subsystem is {subsystem}, expected WINDOWS_GUI (2); console window would appear")
if "无用脑洞研究所".encode("utf-8") in data:
    raise SystemExit("FAILED: developer identity is stored as plain UTF-8")
print("PE CHECK PASS")
print("WINDOWS GUI SUBSYSTEM PASS")
print("DEVELOPER STRING OBFUSCATION PASS")
print("SHA256", hashlib.sha256(data).hexdigest())
