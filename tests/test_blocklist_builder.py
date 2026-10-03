import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BUILDER = Path(__file__).resolve().parents[1] / 'tools' / 'build_blocklist.py'
spec = importlib.util.spec_from_file_location('builder', BUILDER)
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class BlocklistBuilderTests(unittest.TestCase):
    def test_normalization_sorted_hashes_and_deduplication(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'hosts.txt'
            output = Path(folder) / 'list.bin'
            source.write_text('0.0.0.0 WWW.Example.com\nexample.com\n*.ads.example.org\n127.0.0.1 localhost\n! comment\n')
            subprocess.run([sys.executable, str(BUILDER), str(output), str(source)], check=True, capture_output=True)
            data = output.read_bytes()
            hashes = [int.from_bytes(data[i:i+5], 'little') for i in range(0, len(data), 5)]
            self.assertEqual(hashes, sorted([builder.fnv(b'example.com'), builder.fnv(b'ads.example.org')]))

    def test_empty_source_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'empty.txt'
            output = Path(folder) / 'list.bin'
            source.write_text('# no domains\n')
            output.write_bytes(b'original')
            result = subprocess.run([sys.executable, str(BUILDER), str(output), str(source)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), b'original')

    def test_idna_www_and_hosts_are_normalized_like_firmware(self):
        domains, versions = builder.parse_domains(
            '# Version: 2026.1003.1\n'
            '0.0.0.0 WWW.Example.com example.net # comment\n'
            'www.tést.example.\n'
        )
        self.assertEqual(domains, {'example.com', 'example.net', 'xn--tst-bma.example'})
        self.assertEqual(versions, ('2026.1003.1',))

    def test_unsupported_adblock_syntax_is_rejected(self):
        for line in ('||ads.example^', 'ads.example##.banner', 'https://ads.example/path', 'ads.example^$third-party'):
            with self.subTest(line=line):
                with self.assertRaises(builder.BlocklistSourceError):
                    builder.parse_domains(line)

    def test_garbage_source_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'garbage.txt'
            output = Path(folder) / 'list.bin'
            source.write_text('not-a-domain\n')
            output.write_bytes(b'previous')
            with self.assertRaises(builder.BlocklistSourceError):
                builder.build(output, [source])
            self.assertEqual(output.read_bytes(), b'previous')


if __name__ == '__main__':
    unittest.main()
