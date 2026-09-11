#!/usr/bin/env python3
"""Create the portable ZIP without Windows PowerShell Compress-Archive.

Uses Python zipfile + Zip64 and extended-length source paths. This is the
V0.6.5.3 ZipLongPath design carried forward into V0.6.6-alpha2.
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import zipfile

SKIP_DIRS = {"__pycache__"}
SKIP_EXTS = {".pyc", ".pyo"}


def win_long(path: Path) -> str:
    s = os.path.abspath(os.fspath(path))
    if os.name != "nt" or s.startswith("\\\\?\\"):
        return s
    if s.startswith("\\\\"):
        return "\\\\?\\UNC\\" + s[2:]
    return "\\\\?\\" + s


def iter_files(root: Path):
    long_root = win_long(root)
    prefix = root.name
    for dirpath, dirnames, filenames in os.walk(long_root, topdown=True):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            if Path(name).suffix.lower() in SKIP_EXTS:
                continue
            full = Path(dirpath) / name
            # Strip the extended path prefix only for relative-path calculation.
            ordinary = os.fspath(full)
            if os.name == "nt" and ordinary.startswith("\\\\?\\UNC\\"):
                ordinary_relbase = "\\\\" + ordinary[8:]
            elif os.name == "nt" and ordinary.startswith("\\\\?\\"):
                ordinary_relbase = ordinary[4:]
            else:
                ordinary_relbase = ordinary
            rel = os.path.relpath(ordinary_relbase, os.path.abspath(root))
            arc = (Path(prefix) / Path(rel)).as_posix()
            yield full, arc


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("output")
    ns = ap.parse_args()
    root = Path(ns.source).resolve()
    out = Path(ns.output).resolve()
    if not root.is_dir():
        raise SystemExit(f"portable source directory not found: {root}")
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    count = 0
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as zf:
        for src, arc in iter_files(root):
            zf.write(os.fspath(src), arc)
            count += 1
    with zipfile.ZipFile(out, "r", allowZip64=True) as zf:
        bad = zf.testzip()
        if bad:
            raise SystemExit(f"ZIP verification failed at: {bad}")
        names = set(zf.namelist())
        required = {
            f"{root.name}/Crow-DLSS5-Video-Image-Converter-Video.exe",
            f"{root.name}/runtime/nvngx_dlssnr.dll",
            f"{root.name}/auto_depth/.venv/Scripts/python.exe",
        }
        missing = sorted(required - names)
        if missing:
            raise SystemExit("ZIP verification missing required entries: " + ", ".join(missing))
    print(f"Portable ZIP OK: {out}")
    print(f"Files archived: {count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
