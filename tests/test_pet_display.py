"""Exercise animation damage regions through the real bounded display renderer."""
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]


class PetDisplayTest(unittest.TestCase):
    def test_custom_animation_repaints_pixels_above_first_body_region(self):
        if not shutil.which('clang++') or not (ROOT / '.pio/libdeps/jc3636w518c/LovyanGFX/src').exists():
            self.skipTest('Build a display profile and install clang first')
        spec = importlib.util.spec_from_file_location('pet_preview', ROOT / 'tools/render_display_previews.py')
        preview = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(preview)
        data = bytearray(27696)
        data[:9] = b'BPT1' + bytes([48, 48, 4, 4, 6])
        struct.pack_into('<HH', data, 18, 0xf800, 0x001f)
        data[48:] = bytes([0x11]) * (24 * 1152)
        data[48 + 5 * 1152:48 + 6 * 1152] = bytes([0x22]) * 1152
        struct.pack_into('<I', data, 12, zlib.crc32(data[16:]))
        # The first frame has red opaque pixels at logical y=54; the next is
        # blue. That crosses the header/body boundary at y=60 and detects an
        # incomplete dirty-region mask, rather than just parser correctness.
        probe = '''
  if (lgfx::host::pixels()[82 * 360 + 109] != 0xf800) return 8;
  snapshot.animationTimeMs = 1250;
  round_ui::display::setSnapshot(snapshot);
  unsigned secondGuard = 0;
  while (round_ui::display::renderOneRegion(1000 + secondGuard++) && secondGuard < 2000) {}
  if (lgfx::host::pixels()[82 * 360 + 109] != 0x001f) return 9;
'''
        preview.RUNNER_CPP = preview.RUNNER_CPP.replace(
            '  if (!lgfx::host::writePpm(argv[2])) return 5;', probe + '\n  if (!lgfx::host::writePpm(argv[2])) return 5;')
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            asset = build / 'boundary.bpt'
            asset.write_bytes(data)
            binary = preview.compile_host(build, asset)
            result = subprocess.run([str(binary), 'pet-home', str(build / 'pet.ppm'), 'en'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, f'renderer returned {result.returncode}: {result.stderr}')


if __name__ == '__main__':
    unittest.main()
