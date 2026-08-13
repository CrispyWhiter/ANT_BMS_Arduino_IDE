#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE_EXTS = {".go", ".cpp", ".h", ".ino", ".py", ".sh", ".yml", ".yaml"}
FORBIDDEN_MARKERS = ("PC_Source", "Reference4", "Reference5", "Fix1", "Fix2", "Fix3", "Fix4", "Fix5", "Fix6", "Fix7", "Fix8")

errors = []
for path in ROOT.rglob("*"):
    if not path.is_file() or path.suffix.lower() not in SOURCE_EXTS:
        continue
    text = path.read_text(errors="ignore")
    rel = path.relative_to(ROOT)
    if path.resolve() == Path(__file__).resolve():
        continue
    for marker in FORBIDDEN_MARKERS:
        if marker in text:
            errors.append(f"development marker {marker}: {rel}")
    for line_no, line in enumerate(text.splitlines(), 1):
        stripped = line.lstrip()
        is_comment = stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*")
        if is_comment and re.search(r"[\u4e00-\u9fff]", line):
            errors.append(f"Chinese source comment: {rel}:{line_no}")


# Release consistency checks.
release_checks = {
    "firmware version": (ROOT / "firmware" / "src" / "app_config.h", '#define BMS_DISPLAY_FIRMWARE_VERSION "v1.1.0"'),
    "designer version": (ROOT / "designer" / "main.go", 'appVersion = "v1.1.0"'),
    "firmware BMSUI v7": (ROOT / "firmware" / "src" / "ui_runtime" / "bmsui_protocol.h", "ProtocolVersion = 7"),
    "designer BMSUI v7": (ROOT / "designer" / "bmsui_device.go", "bmsUiProtocolVersion       = 7"),
    "README BMSUI v7": (ROOT / "README.md", "BMSUI v7"),
    "examples BMSUI v7": (ROOT / "examples" / "README.md", "BMSUI v7"),
}
for label, (path, token) in release_checks.items():
    if not path.exists() or token not in path.read_text(errors="ignore"):
        errors.append(f"release mismatch {label}: {path.relative_to(ROOT)}")

for path in (ROOT / "docs").glob("*Hotfix*.md"):
    errors.append(f"obsolete release document: {path.relative_to(ROOT)}")

known_dead = {
    ROOT / "designer" / "bmsui_device.go": "exportBmsUiFile",
    ROOT / "designer" / "config.go": "legacyConfigPath",
}
for path, symbol in known_dead.items():
    if path.exists() and re.search(rf"\b{re.escape(symbol)}\b", path.read_text(errors="ignore")):
        errors.append(f"obsolete symbol {symbol}: {path.relative_to(ROOT)}")

if errors:
    raise SystemExit("SOURCE AUDIT FAILED\n" + "\n".join(errors))
print("SOURCE AUDIT PASS")
