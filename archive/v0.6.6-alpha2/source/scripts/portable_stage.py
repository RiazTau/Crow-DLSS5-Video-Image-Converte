#!/usr/bin/env python3
"""Long-path-safe helper for the DLSS5 portable build.

This helper deliberately performs destructive cleanup only inside paths supplied
by make_portable.ps1. It exists because Windows PowerShell 5.1 file cmdlets are
unreliable on the deep ONNX Runtime package tree.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
from datetime import datetime, timezone


def _win_long(path: Path) -> str:
    s = os.path.abspath(os.fspath(path))
    if os.name != "nt" or s.startswith("\\\\?\\"):
        return s
    if s.startswith("\\\\"):
        return "\\\\?\\UNC\\" + s[2:]
    return "\\\\?\\" + s


def remove_tree(path: Path) -> None:
    p = _win_long(path)
    if os.path.exists(p):
        shutil.rmtree(p)


def prune(root: Path) -> None:
    base = Path(_win_long(root))
    # ONNX Runtime developer tools are not used by inference and previously
    # created >260-character paths that broke the final archive stage.
    for rel in (
        Path("onnxruntime") / "tools",
    ):
        target = base / rel
        if target.exists():
            shutil.rmtree(target)

    # Remove bytecode/cache files. Keep actual runtime packages and DLLs.
    for dirpath, dirnames, filenames in os.walk(base, topdown=True):
        cache_dirs = [d for d in dirnames if d == "__pycache__"]
        for d in cache_dirs:
            shutil.rmtree(Path(dirpath) / d, ignore_errors=True)
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        for name in filenames:
            if name.endswith((".pyc", ".pyo")):
                try:
                    os.remove(Path(dirpath) / name)
                except FileNotFoundError:
                    pass


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(_win_long(path), "rb") as f:
        for block in iter(lambda: f.read(4 * 1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_manifest(root: Path, output: Path, version: str) -> None:
    critical = [
        "Crow-DLSS5-Video-Image-Converter-Image.exe",
        "Crow-DLSS5-Video-Image-Converter-Video.exe",
        "Crow-DLSS5-Video-Image-Converter-CLI.exe",
        "runtime/nvngx_dlssnr.dll",
        "video/ffmpeg/bin/ffmpeg.exe",
        "video/ffmpeg/bin/ffprobe.exe",
        "models/depth_anything_v2/model_fp16.onnx",
        "auto_depth/.venv/Scripts/python.exe",
        "auto_depth/.venv/Scripts/msvcp140.dll",
        "auto_depth/.venv/Scripts/vcruntime140.dll",
    ]
    records = []
    total_files = 0
    total_bytes = 0
    long_root = Path(_win_long(root))
    for dirpath, _, filenames in os.walk(long_root):
        for name in filenames:
            p = Path(dirpath) / name
            total_files += 1
            try:
                total_bytes += p.stat().st_size
            except OSError:
                pass
    for rel in critical:
        p = root / Path(rel)
        if p.exists():
            records.append({"path": rel, "bytes": p.stat().st_size, "sha256": sha256(p)})
        else:
            records.append({"path": rel, "missing": True})
    data = {
        "product": "Crow-DLSS5-Video-Image-Converter",
        "version": version,
        "architecture": "x64",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "file_count": total_files,
        "total_bytes": total_bytes,
        "critical_files": records,
        "runtime_notes": {
            "nvof_sdk_bundled": False,
            "nvof_runtime": "Provided by the NVIDIA display driver (nvofapi64.dll)",
            "msvc_runtime": "VC143 CRT bundled app-local to match NVIDIA NGX /MD ABI",
        },
    }
    output.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("remove")
    p.add_argument("path")
    p = sub.add_parser("prune")
    p.add_argument("path")
    p = sub.add_parser("manifest")
    p.add_argument("root")
    p.add_argument("output")
    p.add_argument("version")
    ns = ap.parse_args()
    if ns.cmd == "remove":
        remove_tree(Path(ns.path))
    elif ns.cmd == "prune":
        prune(Path(ns.path))
    else:
        write_manifest(Path(ns.root), Path(ns.output), ns.version)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
