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

    def matrix(self, value, setup=False):
        command = [str(self.binary), value]
        if setup:
            command.append("--setup")
        matrix = subprocess.check_output(command, text=True).splitlines()
        side = 29 if setup else 25
        self.assertEqual(len(matrix), side)
        self.assertTrue(all(len(row) == side and set(row) <= {"0", "1"} for row in matrix))
        return matrix

    def test_station_portal_and_maximum_length_urls(self):
        station = self.matrix("192.168.31.107")
        portal = self.matrix("192.168.4.1")
        longest = self.matrix("223.255.255.255")
        self.assertNotEqual(station, portal)
        self.assertNotEqual(station, longest)

    def test_setup_wifi_uses_version_three_and_can_decode_when_opencv_is_available(self):
        matrix = self.matrix("C3-AdBlock-AB12", setup=True)
        try:
            import cv2
            import numpy as np
        except ImportError:
            self.skipTest("OpenCV decoder is optional")

        side = len(matrix)
        quiet = 4
        scale = 8
        image = np.full(((side + 2 * quiet) * scale,
                         (side + 2 * quiet) * scale), 255, dtype=np.uint8)
        for y, row in enumerate(matrix):
            for x, module in enumerate(row):
                if module == "1":
                    top = (y + quiet) * scale
                    left = (x + quiet) * scale
                    image[top:top + scale, left:left + scale] = 0
        decoded, _, _ = cv2.QRCodeDetector().detectAndDecode(image)
        self.assertEqual(decoded, "WIFI:T:nopass;S:C3-AdBlock-AB12;;")


if __name__ == "__main__":
    unittest.main()
