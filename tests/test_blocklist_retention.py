from datetime import datetime, timedelta, timezone
import unittest
from tools.prune_blocklists import obsolete_assets


class RetentionTests(unittest.TestCase):
    def test_current_latest_three_grace_and_unrelated_assets_are_preserved(self):
        now = datetime(2026, 10, 3, tzinfo=timezone.utc)
        assets = []
        for index, age in enumerate((0, 1, 4, 6, 8, 10)):
            assets.append({'name': 'blocklist-balanced-' + f'{index:064x}' + '.bin',
                           'createdAt': (now - timedelta(days=age)).isoformat()})
        current = assets[-1]['name']  # Explicitly pinned, even older than the grace period.
        manifest = {'profiles': {'balanced': {'asset': current}},
                    'sourceArchive': {'asset': 'hagezi-source-' + 'a' * 40 + '.tar.gz'}}
        assets.append({'name': 'firmware-jc3636w518c.bin', 'createdAt': '2020-01-01T00:00:00Z'})
        self.assertEqual(obsolete_assets(assets, manifest, now), [assets[4]['name']])

    def test_sparse_history_keeps_three_old_generations(self):
        now = datetime(2026, 10, 3, tzinfo=timezone.utc)
        assets = [{'name': 'blocklist-light-' + f'{index:064x}' + '.bin',
                   'createdAt': (now - timedelta(days=20 + index)).isoformat()} for index in range(5)]
        manifest = {'profiles': {}, 'sourceArchive': {'asset': 'current-source.tar.gz'}}
        self.assertEqual(obsolete_assets(assets, manifest, now), sorted(a['name'] for a in assets[3:]))
