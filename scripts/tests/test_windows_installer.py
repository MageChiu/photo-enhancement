"""Exercise generated install scripts without requiring a Windows compiler or WiX."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('cmake'), 'CMake is required')
class WindowsInstallerTests(unittest.TestCase):
    def test_native_backslashes_survive_install_script_generation(self):
        with tempfile.TemporaryDirectory(prefix='photo installer ') as directory:
            base = Path(directory)
            bundle = base / 'app bundle' / 'Release'
            bundle.mkdir(parents=True)
            payload = bundle / 'photo_enhancement.exe'
            payload.write_bytes(b'application fixture')
            assets = bundle / 'data' / 'flutter_assets'
            assets.mkdir(parents=True)
            (assets / 'asset.txt').write_text('asset fixture', encoding='utf-8')
            build = base / 'installer'
            install = base / 'installed application'
            native_path = str(bundle).replace('/', '\\')
            configured = subprocess.run(
                ['cmake', '-S', str(ROOT / 'platform/windows'), '-B', str(build),
                 '-DCMAKE_SYSTEM_NAME=Windows', f'-DPE_APP_DIR={native_path}',
                 '-DPE_APP_VERSION=1.2.3'],
                capture_output=True, text=True,
            )
            self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
            # Check the effective installer metadata, including the database
            # encoding that must represent the real Chinese shortcut labels.
            template = (build / 'WIX.template.in').read_text(encoding='utf-8')
            template = template.replace('@CPACK_WIX_CUSTOM_XMLNS_EXPANDED@',
                                        ' xmlns:ui="http://wixtoolset.org/schemas/v4/wxs/ui"')
            package = ET.fromstring(template).find('{http://wixtoolset.org/schemas/v4/wxs}Package')
            self.assertIsNotNone(package)
            self.assertEqual(package.attrib['Language'], '2052')
            codepage = package.attrib['Codepage']
            label = '\u5149\u5c7f'
            self.assertEqual(label.encode('cp' + codepage).decode('cp' + codepage), label)
            cpack = (build / 'CPackConfig.cmake').read_text(encoding='utf-8')
            self.assertIn('set(CPACK_WIX_CULTURES "zh-CN")', cpack)
            script = (build / 'cmake_install.cmake').read_text(encoding='utf-8')
            self.assertIn(bundle.as_posix() + '/', script)
            installed = subprocess.run(
                ['cmake', '--install', str(build), '--prefix', str(install)],
                capture_output=True, text=True,
            )
            self.assertEqual(installed.returncode, 0, installed.stdout + installed.stderr)
            self.assertNotIn('Invalid escape sequence', installed.stderr)
            self.assertEqual((install / payload.name).read_bytes(), payload.read_bytes())
            self.assertEqual((install / 'data/flutter_assets/asset.txt').read_text(), 'asset fixture')


if __name__ == '__main__':
    unittest.main()
