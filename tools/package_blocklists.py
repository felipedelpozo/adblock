#!/usr/bin/env python3
"""Build and package the three pinned HaGeZi blocklist profiles.

Only the generated hash assets and the HaGeZi GPL notice are emitted.  The
firmware distribution remains MIT licensed and is intentionally not copied
into this list-only release.
"""

from __future__ import annotations

import argparse
import hashlib
import gzip
import io
import json
import os
import shutil
import tarfile
import tempfile
from datetime import datetime, timezone
from pathlib import Path

try:
    from tools import build_blocklist
except ImportError:  # direct invocation from the tools directory
    import build_blocklist  # type: ignore[no-redef]

REPOSITORY = "felipedelpozo/adblock"
RELEASE_TAG = "blocklists"
HAGEZI_REPOSITORY = "https://github.com/hagezi/dns-blocklists"
HAGEZI_LICENSE_URL = f"{HAGEZI_REPOSITORY}/blob/main/LICENSE"
HAGEZI_LICENSE = "GPL-3.0"
HAGEZI_LICENSE_FILENAME = "LICENSE-HAGEZI-GPL-3.0.txt"
HAGEZI_NOTICE_FILENAME = "NOTICE-HAGEZI.txt"


def _utc_iso(value: str | datetime | None) -> str:
    if value is None:
        current = datetime.now(timezone.utc)
    elif isinstance(value, datetime):
        current = value
    else:
        return value
    if current.tzinfo is None:
        current = current.replace(tzinfo=timezone.utc)
    return current.astimezone(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _source_url(profile: str, commit: str) -> str:
    path = build_blocklist.PROFILE_SPECS[profile]["path"]
    return f"https://cdn.jsdelivr.net/gh/hagezi/dns-blocklists@{commit}/wildcard/{path}"


def _license_url(commit: str) -> str:
    return f"{HAGEZI_REPOSITORY}/blob/{commit}/LICENSE"


def _resolve_sources(source_dir: Path | None, source_commit: str | None) -> tuple[dict[str, str | Path], str]:
    commit = source_commit or ("local-fixture" if source_dir else "latest")
    if source_dir:
        sources: dict[str, str | Path] = {}
        for profile in build_blocklist.PROFILE_SPECS:
            candidate = source_dir / f"{profile}.txt"
            if not candidate.exists():
                raise ValueError(f"missing source fixture: {candidate}")
            sources[profile] = candidate
        return sources, commit
    return {
        profile: _source_url(profile, commit)
        for profile in build_blocklist.PROFILE_SPECS
    }, commit


def _validate_binary(path: Path, max_domains: int) -> tuple[bytes, int]:
    data = path.read_bytes()
    if not data or len(data) % build_blocklist.HASH_BYTES:
        raise ValueError(f"{path.name}: binary is empty or not a multiple of five bytes")
    values = [
        int.from_bytes(data[index:index + build_blocklist.HASH_BYTES], "little")
        for index in range(0, len(data), build_blocklist.HASH_BYTES)
    ]
    if values != sorted(set(values)):
        raise ValueError(f"{path.name}: hashes must be sorted and unique")
    domains = len(values)
    if domains > max_domains:
        raise ValueError(f"{path.name}: {domains:,} domains exceed budget {max_domains:,}")
    return data, domains


def _read_license(path: Path | None) -> str:
    if path is None:
        raise ValueError("--license-path is required for a network package")
    text = path.read_text(encoding="utf-8")
    if "GNU GENERAL PUBLIC LICENSE" not in text or "Version 3" not in text:
        raise ValueError(f"{path}: expected the official GPL-3.0 license text")
    return text


def package(
    output: str | os.PathLike[str],
    *,
    source_dir: str | os.PathLike[str] | None = None,
    source_commit: str | None = None,
    license_path: str | os.PathLike[str] | None = None,
    generated_at: str | datetime | None = None,
) -> dict:
    """Build all profiles and atomically publish package files into output.

    Sources are fully built in a temporary directory first.  Existing output
    files and the previous manifest are untouched if any download, parse,
    budget or validation step fails.  The manifest is replaced last.
    """
    destination = Path(output)
    fixture_dir = Path(source_dir) if source_dir is not None else None
    sources, commit = _resolve_sources(fixture_dir, source_commit)
    license_text = _read_license(Path(license_path) if license_path else None)
    generated = _utc_iso(generated_at)
    stage_parent = destination.parent if destination.parent.exists() else Path.cwd()
    stage: Path | None = Path(tempfile.mkdtemp(prefix=f".{destination.name}.", dir=stage_parent))
    try:
        profiles: dict[str, dict] = {}
        staged_assets: list[tuple[Path, Path]] = []
        source_files: list[tuple[str, bytes]] = []
        for profile, spec in build_blocklist.PROFILE_SPECS.items():
            # Snapshot the exact source bytes used for conversion.  This is
            # included in the release source archive to satisfy GPL source
            # form and to make a package independently auditable.
            source_text = build_blocklist.read_source(sources[profile])
            source_name = f"hagezi-{profile}.txt"
            source_files.append((source_name, source_text.encode("utf-8")))
            source_snapshot = stage / source_name
            source_snapshot.write_text(source_text, encoding="utf-8")
            built = stage / f"{profile}.bin"
            result = build_blocklist.build(built, [source_snapshot], spec["max_domains"])
            data, domains = _validate_binary(built, spec["max_domains"])
            digest = hashlib.sha256(data).hexdigest()
            asset = f"blocklist-{profile}-{digest}.bin"
            staged_asset = stage / asset
            shutil.copyfile(built, staged_asset)
            staged_assets.append((staged_asset, destination / asset))
            source = {
                "name": spec["name"],
                "url": _source_url(profile, commit),
                "license": HAGEZI_LICENSE,
                "licenseUrl": _license_url(commit),
                "repository": HAGEZI_REPOSITORY,
                "commit": commit,
                "sourceForm": source_name,
            }
            if result.source_versions:
                source["version"] = result.source_versions[-1]
            profiles[profile] = {
                "asset": asset,
                "url": f"https://github.com/{REPOSITORY}/releases/download/{RELEASE_TAG}/{asset}",
                "sha256": digest,
                "size": len(data),
                "domains": domains,
                "source": source,
            }

        notice = (
            "HaGeZi DNS Blocklists source notice\n"
            "===================================\n\n"
            f"Source repository: {HAGEZI_REPOSITORY}\n"
            f"Pinned source commit: {commit}\n"
            f"License: GNU GPL-3.0 ({_license_url(commit)})\n\n"
            "The three assets in this release are derived from HaGeZi's "
            "domain-only Multi LIGHT, Multi PRO and Multi PRO++ lists. "
            "The ESP32 firmware is a separate MIT-licensed distribution and "
            "is not included in this list release.\n"
        )
        (stage / HAGEZI_LICENSE_FILENAME).write_text(license_text, encoding="utf-8")
        (stage / HAGEZI_NOTICE_FILENAME).write_text(notice, encoding="utf-8")
        source_archive_name = f"hagezi-source-{commit}.tar.gz"
        source_archive_stage = stage / source_archive_name
        # Deterministic tar/gzip metadata keeps the source archive stable for
        # the same pinned inputs.  It contains the exact three source texts,
        # this notice, and the corresponding GPL text.
        with source_archive_stage.open("wb") as archive_file:
            with gzip.GzipFile(fileobj=archive_file, mode="wb", mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w") as archive:
                    archive_entries = list(source_files) + [
                        (HAGEZI_NOTICE_FILENAME, notice.encode("utf-8")),
                        (HAGEZI_LICENSE_FILENAME, license_text.encode("utf-8")),
                    ]
                    for name, payload in archive_entries:
                        info = tarfile.TarInfo(name)
                        info.size = len(payload)
                        info.mode = 0o644
                        info.mtime = 0
                        archive.addfile(info, io.BytesIO(payload))
        source_archive_data = source_archive_stage.read_bytes()
        source_archive_digest = hashlib.sha256(source_archive_data).hexdigest()
        source_archive_name = f"hagezi-source-{commit}-{source_archive_digest}.tar.gz"
        manifest = {
            "schema": 1,
            "repository": REPOSITORY,
            "generatedAt": generated,
            "sourceArchive": {
                "asset": source_archive_name,
                "url": f"https://github.com/{REPOSITORY}/releases/download/{RELEASE_TAG}/{source_archive_name}",
                "sha256": source_archive_digest,
                "size": len(source_archive_data),
                "commit": commit,
            },
            "profiles": profiles,
        }
        manifest_stage = stage / "manifest.json"
        manifest_stage.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        sums_stage = stage / "SHA256SUMS"
        sums_stage.write_text(
            "".join(f"{profile['sha256']}  {profile['asset']}\n" for profile in profiles.values())
            + f"{source_archive_digest}  {source_archive_name}\n",
            encoding="utf-8",
        )

        destination.mkdir(parents=True, exist_ok=True)
        # Assets and notices are independently content addressed or immutable;
        # this also leaves previous generations available for rollback.
        for source, target in staged_assets:
            os.replace(source, target)
        os.replace(source_archive_stage, destination / source_archive_name)
        for filename in (HAGEZI_LICENSE_FILENAME, HAGEZI_NOTICE_FILENAME, "SHA256SUMS"):
            os.replace(stage / filename, destination / filename)
        # Keep this last: consumers never observe a manifest that references a
        # binary which has not yet been installed.
        os.replace(manifest_stage, destination / "manifest.json")
        return manifest
    finally:
        if stage is not None:
            shutil.rmtree(stage, ignore_errors=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("release/blocklists"))
    parser.add_argument("--source-dir", type=Path, help="offline directory containing light.txt, balanced.txt and strict.txt")
    parser.add_argument("--source-commit", required=True, help="pinned HaGeZi dns-blocklists Git commit")
    parser.add_argument("--license-path", type=Path, required=True, help="official HaGeZi GPL-3.0 license file")
    parser.add_argument("--generated-at", help="UTC ISO timestamp (defaults to now)")
    args = parser.parse_args(argv)
    try:
        package(args.output, source_dir=args.source_dir, source_commit=args.source_commit, license_path=args.license_path, generated_at=args.generated_at)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Blocklist packaging failed: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
