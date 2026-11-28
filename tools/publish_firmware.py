#!/usr/bin/env python3
"""Copy the firmware that was just built into docs/firmware/, where the web
console (docs/, GitHub Pages) picks it up for flashing from the browser.

    pio run                         # build first
    python3 tools/publish_firmware.py
    git add docs/firmware && git commit && git push

Commit the source changes first: the manifest records the commit the binaries
were built from.
"""
import datetime
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / ".pio" / "build" / "kero"
OUT = ROOT / "docs" / "firmware"

# Flash layout of the ESP32-S3 build (see .pio/build/kero/flasher_args.json).
PARTS = [
    ("bootloader.bin", 0x0),
    ("partitions.bin", 0x8000),
    ("firmware.bin", 0x10000),
]


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def main():
    missing = [name for name, _ in PARTS if not (BUILD / name).is_file()]
    if missing:
        sys.exit("not built yet (run 'pio run'): missing " + ", ".join(missing))
    OUT.mkdir(parents=True, exist_ok=True)
    parts = []
    for name, offset in PARTS:
        data = (BUILD / name).read_bytes()
        shutil.copyfile(BUILD / name, OUT / name)
        parts.append({"file": name, "offset": offset, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    dirty = bool(git("status", "--porcelain", "--", "main", "components", "sdkconfig.defaults", "platformio.ini"))
    manifest = {
        "name": "kero-firmware",
        "chip": "ESP32-S3",
        "version": git("rev-parse", "--short", "HEAD") + ("+" if dirty else ""),
        "built": datetime.date.today().isoformat(),
        "parts": parts,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("published", manifest["version"], "to", OUT.relative_to(ROOT))
    if dirty:
        print("note: the source tree has uncommitted changes, the version is marked with '+'")


if __name__ == "__main__":
    main()
