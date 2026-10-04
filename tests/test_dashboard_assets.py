"""Verify that compressed firmware assets preserve the readable web source."""
import gzip
import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DashboardAssetTest(unittest.TestCase):
    def test_dashboard_gzip_roundtrip_and_deterministic_header(self):
        spec = importlib.util.spec_from_file_location("dashboard_builder", ROOT / "tools/build_dashboard_page.py")
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        expected = builder.generate()
        self.assertEqual((ROOT / "src/page_gzip.h").read_text(), expected)
        binary = bytes(int(byte, 16) for byte in re.findall(r"0x([0-9a-f]{2})", expected))
        source = (ROOT / "src/page.h").read_text().split('R"HTML(', 1)[1].rsplit(')HTML"', 1)[0]
        self.assertEqual(gzip.decompress(binary).decode(), source)
        self.assertEqual(binary[4:8], bytes(4))
        self.assertLess(len(binary), len(source.encode()) // 2)


if __name__ == "__main__":
    unittest.main()
