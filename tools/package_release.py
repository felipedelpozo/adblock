#!/usr/bin/env python3
"""Validate and package OTA application images, never bootloader/filesystem images."""
import argparse
import hashlib
import json
import re
from pathlib import Path

PROFILES = {
    "c3": ("ESP32-C3", "esp32-c3-devkitm-1", 5, 0x150000),
    "round-display": ("ESP32-C3", "esp32-c3-devkitm-1", 5, 0x150000),
    "s3-headless": ("ESP32-S3", "jc3636w518c", 9, 0x200000),
    "jc3636w518c": ("ESP32-S3", "jc3636w518c", 9, 0x200000),
}
VERSION_PATTERN = r"(?:0|[1-9][0-9]{0,5})\.(?:0|[1-9][0-9]{0,5})\.(?:0|[1-9][0-9]{0,5})"
REPOSITORY = "felipedelpozo/adblock"


def package(version, build_root, output):
    if not re.fullmatch(VERSION_PATTERN, version):
        raise ValueError("version must be a stable major.minor.patch version without v")
    if (Path(__file__).resolve().parents[1] / "src/secrets.h").exists():
        raise ValueError("Remove compile-time secrets and rebuild before packaging public firmware")
    images = {}
    builds = []
    for profile, (chip, board, chip_id, slot_size) in PROFILES.items():
        source = build_root / profile / "firmware.bin"
        data = source.read_bytes()
        if len(data) < 24 or data[0] != 0xE9 or int.from_bytes(data[12:14], "little") != chip_id:
            raise ValueError(f"{profile}: invalid application image or wrong ESP32 chip")
        if len(data) > slot_size:
            raise ValueError(f"{profile}: firmware exceeds its OTA partition")
        marker = f"ADBLOCK_ID:{profile}:{version}\0".encode("ascii")
        if marker not in data:
            raise ValueError(f"{profile}: compiled firmware identity/version does not match {version}")
        asset = f"firmware-{profile}.bin"
        images[asset] = data
        builds.append({
            "profile": profile, "version": version, "chip": chip, "board": board,
            "asset": asset,
            "url": f"https://github.com/{REPOSITORY}/releases/download/v{version}/{asset}",
            "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
        })
    output.mkdir(parents=True, exist_ok=True)
    for asset, data in images.items():
        (output / asset).write_bytes(data)
    manifest = {"schema": 1, "repository": REPOSITORY, "version": version, "builds": builds}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "SHA256SUMS").write_text("".join(f"{b['sha256']}  {b['asset']}\n" for b in builds))
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-root", type=Path, default=Path(".pio/build"))
    parser.add_argument("--output", type=Path, default=Path("release"))
    args = parser.parse_args()
    try:
        package(args.version, args.build_root, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Release validation failed: {error}\n")


if __name__ == "__main__":
    main()
