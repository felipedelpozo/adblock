"""Exercise the real bundled C QR encoder through the firmware's C++ adapter.

Build a display PlatformIO profile first to populate .pio/libdeps. No Python
image libraries are needed; external scanning is a separate physical check.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class DisplayQrTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        libraries = list((ROOT / ".pio/libdeps").glob("*/LovyanGFX/src"))
        cc = shutil.which("clang") or shutil.which("gcc")
        cxx = shutil.which("clang++") or shutil.which("g++")
        if not libraries or not cc or not cxx:
            raise unittest.SkipTest("Build a display profile and install a C/C++ host compiler first")
        cls.scratch = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.scratch.cleanup)
        directory = pathlib.Path(cls.scratch.name)
        obj = directory / "qrcode.o"
        cls.binary = directory / "verify-qr"
        subprocess.run([cc, "-c", str(libraries[0] / "lgfx/utility/lgfx_qrcode.c"), "-o", str(obj)], check=True)
        subprocess.run([cxx, "-std=c++17", "-DROUND_DISPLAY=1", f"-I{ROOT / 'src'}",
                        f"-I{libraries[0]}", str(ROOT / "tests/display_qr_harness.cpp"),
                        str(ROOT / "src/display_qr.cpp"), str(obj), "-o", str(cls.binary)], check=True)

    def matrix(self, ip):
        matrix = subprocess.check_output([str(self.binary), ip], text=True).splitlines()
        self.assertEqual(len(matrix), 25)
        self.assertTrue(all(len(row) == 25 and set(row) <= {"0", "1"} for row in matrix))
        return matrix

    def test_station_portal_and_maximum_length_urls(self):
        station = self.matrix("192.168.31.107")
        portal = self.matrix("192.168.4.1")
        longest = self.matrix("223.255.255.255")
        self.assertNotEqual(station, portal)
        self.assertNotEqual(station, longest)


if __name__ == "__main__":
    unittest.main()
