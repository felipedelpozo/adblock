import hashlib
import importlib.util
import json
import tarfile
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools' / 'package_blocklists.py'
spec = importlib.util.spec_from_file_location('package_blocklists', SCRIPT)
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class BlocklistPackagingTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.sources = self.root / 'sources'
        self.sources.mkdir()
        for profile in ('light', 'balanced', 'strict'):
            (self.sources / f'{profile}.txt').write_text(
                f'# Title: test {profile}\n# Version: test-{profile}\n'
                'www.ads.example\ntracker.example\n0.0.0.0 malware.example\n'
            )
        self.license = self.root / 'LICENSE'
        self.license.write_text('GNU GENERAL PUBLIC LICENSE\nVersion 3, 29 June 2007\n')

    def test_manifest_assets_and_source_archive_are_self_consistent(self):
        output = self.root / 'release'
        manifest = packager.package(
            output,
            source_dir=self.sources,
            source_commit='0123456789abcdef0123456789abcdef01234567',
            license_path=self.license,
            generated_at='2026-10-03T00:00:00Z',
        )
        self.assertEqual(manifest['schema'], 1)
        self.assertEqual(manifest['repository'], 'felipedelpozo/adblock')
        self.assertEqual(manifest['generatedAt'], '2026-10-03T00:00:00Z')
        self.assertEqual(set(manifest['profiles']), {'light', 'balanced', 'strict'})
        for profile, entry in manifest['profiles'].items():
            self.assertRegex(entry['asset'], rf'^blocklist-{profile}-[0-9a-f]{{64}}\.bin$')
            self.assertEqual(entry['url'], f"https://github.com/felipedelpozo/adblock/releases/download/blocklists/{entry['asset']}")
            data = (output / entry['asset']).read_bytes()
            self.assertEqual(entry['sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(entry['size'], len(data))
            self.assertEqual(entry['domains'], len(data) // 5)
            self.assertEqual(entry['source']['license'], 'GPL-3.0')
            self.assertEqual(entry['source']['commit'], '0123456789abcdef0123456789abcdef01234567')
            self.assertTrue(entry['source']['sourceForm'].endswith('.txt'))
        archive_info = manifest['sourceArchive']
        archive = output / archive_info['asset']
        self.assertEqual(archive_info['sha256'], hashlib.sha256(archive.read_bytes()).hexdigest())
        with tarfile.open(archive, 'r:gz') as tar:
            self.assertEqual(
                set(tar.getnames()),
                {'hagezi-light.txt', 'hagezi-balanced.txt', 'hagezi-strict.txt', 'NOTICE-HAGEZI.txt', 'LICENSE-HAGEZI-GPL-3.0.txt'},
            )
        self.assertIn('GPL-3.0', (output / 'NOTICE-HAGEZI.txt').read_text())

    def test_failed_source_does_not_replace_existing_manifest(self):
        output = self.root / 'release'
        packager.package(output, source_dir=self.sources, source_commit='first', license_path=self.license, generated_at='2026-10-03T00:00:00Z')
        old_manifest = (output / 'manifest.json').read_bytes()
        (self.sources / 'strict.txt').write_text('||unsafe.example^\n')
        with self.assertRaises(ValueError):
            packager.package(output, source_dir=self.sources, source_commit='second', license_path=self.license, generated_at='2026-10-04T00:00:00Z')
        self.assertEqual((output / 'manifest.json').read_bytes(), old_manifest)


if __name__ == '__main__':
    unittest.main()
