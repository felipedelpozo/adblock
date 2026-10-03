import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[1] / 'tools/package_release.py'
spec = importlib.util.spec_from_file_location('package_release', SCRIPT)
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


def image(profile, version='1.2.3'):
    data = bytearray(24)
    data[0] = 0xE9
    data[12:14] = packager.PROFILES[profile][2].to_bytes(2, 'little')
    return bytes(data) + f'ADBLOCK_ID:{profile}:{version}\0'.encode()


class ReleasePackagingTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / 'build'
        self.out = Path(self.tmp.name) / 'release'
        for profile in packager.PROFILES:
            path = self.root / profile
            path.mkdir(parents=True)
            (path / 'firmware.bin').write_bytes(image(profile))

    def test_manifest_has_exact_profiles_and_checksums(self):
        manifest = packager.package('1.2.3', self.root, self.out)
        self.assertEqual({b['profile'] for b in manifest['builds']}, set(packager.PROFILES))
        for build in manifest['builds']:
            data = (self.out / build['asset']).read_bytes()
            self.assertEqual(build['size'], len(data))
            self.assertEqual(build['sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(build['url'], f"https://github.com/felipedelpozo/adblock/releases/download/v1.2.3/{build['asset']}")

    def test_rejects_wrong_compiled_version_without_partial_output(self):
        (self.root / 'jc3636w518c/firmware.bin').write_bytes(image('jc3636w518c', '1.2.2'))
        with self.assertRaises(ValueError):
            packager.package('1.2.3', self.root, self.out)
        self.assertFalse(self.out.exists())

    def test_rejects_swapped_profiles_and_wrong_chip(self):
        path = self.root / 'c3/firmware.bin'
        path.write_bytes(image('round-display'))
        with self.assertRaises(ValueError):
            packager.package('1.2.3', self.root, self.out)
        path.write_bytes(image('jc3636w518c'))
        with self.assertRaises(ValueError):
            packager.package('1.2.3', self.root, self.out)

    def test_rejects_oversize_and_invalid_versions(self):
        for version in ('main', 'v1.2.3', '1.2.3-beta', '1.02.3', '9999999.0.0'):
            with self.assertRaises(ValueError):
                packager.package(version, self.root, self.out)
        path = self.root / 'c3/firmware.bin'
        path.write_bytes(image('c3') + b'\0' * 0x150000)
        with self.assertRaises(ValueError):
            packager.package('1.2.3', self.root, self.out)


if __name__ == '__main__':
    unittest.main()
