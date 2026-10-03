#!/usr/bin/env python3
"""Build the ESP32 40-bit hash blocklist from domain-only or hosts sources.

For compatibility with older generated lists, a leading ``www.`` in a source
rule is normalized to its parent domain. The firmware also checks parent
domains, while preserving the requested name for precise allowlist exceptions.
It deliberately does not implement Adblock Plus syntax: cosmetic rules,
modifiers, URL paths and element hiding rules are rejected instead of being
silently converted into a broader domain rule.
"""

from __future__ import annotations

import argparse
import ipaddress
import os
import re
import tempfile
import urllib.request
from pathlib import Path
from typing import Iterable, Sequence

HASH_BYTES = 5
MASK = (1 << (HASH_BYTES * 8)) - 1
FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
U64 = (1 << 64) - 1
URL_TIMEOUT_SECONDS = 60

HAGEZI_REPOSITORY = "https://github.com/hagezi/dns-blocklists"
HAGEZI_LICENSE_URL = f"{HAGEZI_REPOSITORY}/blob/main/LICENSE"
HAGEZI_RAW_BASE = "https://cdn.jsdelivr.net/gh/hagezi/dns-blocklists@latest/wildcard"

# Bound source growth to the firmware's 2 MiB per-list limit. C3 devices also
# check their actual free staging space and reject oversized replacements.
PROFILE_SPECS = {
    "light": {"name": "HaGeZi Multi LIGHT", "path": "light-onlydomains.txt", "max_domains": 400_000},
    "balanced": {"name": "HaGeZi Multi PRO", "path": "pro-onlydomains.txt", "max_domains": 400_000},
    "strict": {"name": "HaGeZi Multi PRO++", "path": "pro.plus-onlydomains.txt", "max_domains": 400_000},
}
PROFILE_URLS = {
    profile: f"{HAGEZI_RAW_BASE}/{spec['path']}"
    for profile, spec in PROFILE_SPECS.items()
}

_DOMAIN_LABEL = re.compile(r"^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$")


class BlocklistSourceError(ValueError):
    """Raised when a source cannot be safely interpreted as domains."""


class BuildResult:
    __slots__ = ("source_domains", "domains", "collisions", "source_versions")

    def __init__(self, source_domains: int, domains: int, collisions: int, source_versions: tuple[str, ...]):
        self.source_domains = source_domains
        self.domains = domains
        self.collisions = collisions
        self.source_versions = source_versions


def fnv(value: bytes) -> int:
    """Return the firmware-compatible truncated FNV-1a hash."""
    result = FNV_OFFSET
    for byte in value:
        result = ((result ^ byte) * FNV_PRIME) & U64
    return result & MASK


def normalize_domain(value: str) -> str:
    """Normalize one DNS domain using ASCII/IDNA and firmware conventions."""
    domain = value.strip().lower()
    if domain.startswith("*."):
        domain = domain[2:]
    if domain.startswith("www."):
        # Retain the original list compiler's parent-domain convention.
        domain = domain[4:]
    domain = domain.rstrip(".")
    if not domain or any(ord(char) < 32 or ord(char) == 127 for char in domain):
        raise BlocklistSourceError(f"invalid domain {value!r}")
    if "/" in domain or "?" in domain or "#" in domain or "^" in domain or "$" in domain:
        raise BlocklistSourceError(f"unsupported URL/Adblock syntax {value!r}")
    try:
        ascii_domain = domain.encode("idna").decode("ascii")
    except UnicodeError as error:
        raise BlocklistSourceError(f"invalid IDNA domain {value!r}") from error
    if len(ascii_domain) > 253 or "." not in ascii_domain:
        raise BlocklistSourceError(f"domain must contain a DNS dot: {value!r}")
    try:
        if ipaddress.ip_address(ascii_domain):
            raise BlocklistSourceError(f"IP address is not a domain: {value!r}")
    except ValueError:
        pass
    labels = ascii_domain.split(".")
    if any(not _DOMAIN_LABEL.fullmatch(label) for label in labels):
        raise BlocklistSourceError(f"invalid DNS label {value!r}")
    return ascii_domain


# Compatibility for callers of the original converter API.
def norm(value: str) -> str:
    return normalize_domain(value)


def _is_ip(value: str) -> bool:
    try:
        ipaddress.ip_address(value)
        return True
    except ValueError:
        return False


def _parse_line(line: str, line_number: int) -> set[str]:
    line = line.lstrip("\ufeff").strip()
    if not line or line.startswith("#") or line.startswith("!"):
        return set()
    if line.startswith("[") or line.startswith("/") or line.startswith("@@") or "##" in line:
        raise BlocklistSourceError(f"line {line_number}: unsupported filter syntax")
    # An inline # is a hosts-file comment.  A # elsewhere in a one-token
    # Adblock rule is rejected by normalize_domain below.
    line = line.split("#", 1)[0].strip()
    if not line:
        return set()
    parts = line.split()
    if _is_ip(parts[0]):
        if len(parts) < 2:
            raise BlocklistSourceError(f"line {line_number}: host entry has no domain")
        domains: set[str] = set()
        for candidate in parts[1:]:
            try:
                domains.add(normalize_domain(candidate))
            except BlocklistSourceError as error:
                # Hosts files commonly contain localhost aliases.  They are
                # not DNS domains and must not make an otherwise valid list
                # fail, while every real domain remains strictly validated.
                if candidate.lower() in {"localhost", "localhost.localdomain", "broadcasthost"}:
                    continue
                raise BlocklistSourceError(f"line {line_number}: {error}") from error
        return domains
    if len(parts) != 1:
        raise BlocklistSourceError(f"line {line_number}: unsupported hosts/filter syntax")
    try:
        return {normalize_domain(parts[0])}
    except BlocklistSourceError as error:
        raise BlocklistSourceError(f"line {line_number}: {error}") from error


def parse_domains(text: str) -> tuple[set[str], tuple[str, ...]]:
    """Parse safe domain/hosts input and return domains plus source versions."""
    domains: set[str] = set()
    versions: set[str] = set()
    for line_number, original in enumerate(text.splitlines(), 1):
        match = re.match(r"^\s*[#!]\s*Version:\s*(\S+)", original, re.IGNORECASE)
        if match:
            versions.add(match.group(1))
        domains.update(_parse_line(original, line_number))
    if not domains:
        raise BlocklistSourceError("source contains no valid domains")
    return domains, tuple(sorted(versions))


def read_source(source: str | os.PathLike[str]) -> str:
    """Read a local source or HTTPS URL with a bounded timeout."""
    source_text = os.fspath(source)
    path = Path(source_text)
    if path.exists():
        return path.read_text(encoding="utf-8-sig")
    if not source_text.startswith("https://"):
        raise BlocklistSourceError(f"source must be a local file or HTTPS URL: {source_text}")
    request = urllib.request.Request(source_text, headers={"User-Agent": "felipedelpozo-adblock/1"})
    try:
        with urllib.request.urlopen(request, timeout=URL_TIMEOUT_SECONDS) as response:
            return response.read().decode("utf-8-sig")
    except Exception as error:
        raise BlocklistSourceError(f"unable to read {source_text}: {error}") from error


def build(output: str | os.PathLike[str], sources: Sequence[str | os.PathLike[str]], max_domains: int | None = None) -> BuildResult:
    """Build one binary atomically, preserving an existing output on failure."""
    if not sources:
        raise BlocklistSourceError("at least one source is required")
    all_domains: set[str] = set()
    versions: set[str] = set()
    for source in sources:
        domains, source_versions = parse_domains(read_source(source))
        all_domains.update(domains)
        versions.update(source_versions)
    if max_domains is not None and len(all_domains) > max_domains:
        raise BlocklistSourceError(f"{len(all_domains):,} domains exceed budget {max_domains:,}")
    hashes = [fnv(domain.encode("ascii")) for domain in all_domains]
    unique_hashes = sorted(set(hashes))
    if not unique_hashes:
        raise BlocklistSourceError("source contains no hashable domains")
    destination = Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile("wb", dir=destination.parent, prefix=f".{destination.name}.", suffix=".tmp", delete=False) as temporary:
            temporary_name = temporary.name
            for value in unique_hashes:
                temporary.write(value.to_bytes(HASH_BYTES, "little"))
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_name, destination)
        temporary_name = None
    finally:
        if temporary_name:
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass
    return BuildResult(
        source_domains=len(all_domains),
        domains=len(unique_hashes),
        collisions=len(hashes) - len(unique_hashes),
        source_versions=tuple(sorted(versions)),
    )


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default="blocklist.bin")
    parser.add_argument("sources", nargs="*", help="local domain/hosts files or HTTPS URLs")
    args = parser.parse_args(list(argv) if argv is not None else None)
    sources = args.sources or [PROFILE_URLS["balanced"]]
    try:
        result = build(args.output, sources)
    except (OSError, BlocklistSourceError) as error:
        parser.exit(1, f"Blocklist build failed: {error}; output left unchanged\n")
    print(f"source domains   : {result.source_domains:,}")
    print(f"hash entries     : {result.domains:,} ({HASH_BYTES}-byte / {HASH_BYTES * 8}-bit)")
    print(f"collisions       : {result.collisions}")
    print(f"flash blob       : {result.domains * HASH_BYTES:,} bytes -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
