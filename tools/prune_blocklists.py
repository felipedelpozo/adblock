#!/usr/bin/env python3
"""Prune only generated list assets after publication, retaining a grace period."""
import argparse
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import re
import subprocess


def obsolete_assets(assets, manifest, now):
    protected = {item['asset'] for item in manifest['profiles'].values()}
    protected.add(manifest['sourceArchive']['asset'])
    groups = {}
    for asset in assets:
        match = re.fullmatch(r'blocklist-(light|balanced|strict)-[0-9a-f]{64}\.bin', asset['name'])
        group = match.group(1) if match else None
        if re.fullmatch(r'hagezi-source-[0-9a-f]{40}(?:-[0-9a-f]{64})?\.tar\.gz', asset['name']):
            group = 'source'
        if group is None:
            continue
        try:
            created = datetime.fromisoformat(asset['createdAt'].replace('Z', '+00:00'))
            if created.tzinfo is None:
                continue
        except (KeyError, ValueError):
            continue
        groups.setdefault(group, []).append((created, asset['name']))
    obsolete = []
    for entries in groups.values():
        ordered = sorted(entries, reverse=True)
        protected.update(name for _, name in ordered[:3])
        obsolete.extend(name for created, name in ordered
                        if name not in protected and created < now - timedelta(days=7))
    return sorted(obsolete)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    if manifest.get('repository') != args.repo:
        parser.error('manifest repository does not match')
    command = ['gh', 'release', 'view', 'blocklists', '--repo', args.repo, '--json', 'assets']
    assets = json.loads(subprocess.check_output(command))['assets']
    names = obsolete_assets(assets, manifest, datetime.now(timezone.utc))
    for name in names:
        subprocess.run(['gh', 'release', 'delete-asset', 'blocklists', name,
                        '--repo', args.repo, '--yes'], check=True)
    print(f'Pruned {len(names)} stale generated assets; current, latest three and seven-day grace retained')


if __name__ == '__main__':
    main()
