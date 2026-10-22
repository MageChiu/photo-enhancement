"""Offline checks for independent platform publication; no GitHub requests."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
from prepare_release import PLATFORM_PACKAGES, prepare

ENV = {
    'GITHUB_SHA': 'a' * 40,
    'GITHUB_REF': 'refs/tags/v0.1.0',
    'GITHUB_REF_TYPE': 'tag',
    'GITHUB_REF_NAME': 'v0.1.0',
    'GITHUB_SERVER_URL': 'https://github.com',
    'GITHUB_REPOSITORY': 'example/photo',
    'GITHUB_RUN_ID': '123',
    'GITHUB_RUN_ATTEMPT': '1',
}
WINDOWS_FILES = {
    'photo_enhancement.exe', 'photo_core.dll', 'flutter_windows.dll',
    'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
    'data/icudtl.dat', 'data/app.so',
    'data/flutter_assets/assets/presets/builtin.json', 'NOTICE.txt',
}
FAKE_GH = '''#!/usr/bin/env python3
import hashlib, json, os
from pathlib import Path
import sys
state_file = Path(os.environ['MOCK_GH_STATE'])
state = json.loads(state_file.read_text())
args = sys.argv[1:]
state['events'].append(args)
result = 0
if args[:2] == ['release', 'view']:
    result = 0 if state['exists'] else 1
elif args[:2] == ['release', 'create']:
    state['exists'] = True
    result = 1 if state.get('create_race') else 0
elif args[:2] == ['release', 'upload']:
    names = [x for x in args[3:] if not x.startswith('--')]
    if any(state.get('fail_platform', '!') in x for x in names):
        result = 2
    else:
        for name in names:
            path = Path(name)
            state['assets'][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
elif args[:2] == ['release', 'edit']:
    state['draft'] = False
else:
    result = 99
state_file.write_text(json.dumps(state))
sys.exit(result)
'''


class IndependentReleaseTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='photo-release-tests-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.output = self.root / 'artifacts'
        self.output.mkdir()
        scripts = self.root / 'scripts'
        scripts.mkdir()
        for name in ['publish_release.sh', 'prepare_release.py']:
            shutil.copy2(SCRIPTS / name, scripts / name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        gh = self.bin / 'gh'
        gh.write_text(FAKE_GH)
        gh.chmod(0o755)
        self.state_file = self.root / 'github.json'
        self.state_file.write_text(json.dumps({
            'exists': True, 'draft': False,
            'assets': {'other-platform-existing.zip': 'untouched'}, 'events': [],
        }))
        self.env = patch.dict(os.environ, ENV)
        self.env.start()
        self.addCleanup(self.env.stop)

    def packages(self, platform):
        for name in PLATFORM_PACKAGES[platform]:
            path = self.output / name
            if path.suffix == '.msi':
                path.write_bytes(bytes.fromhex('d0cf11e0a1b11ae1') + b'fixture')
            elif path.suffix == '.dmg':
                path.write_bytes(b'validated-on-macos')
            else:
                with zipfile.ZipFile(path, 'w') as archive:
                    for file in WINDOWS_FILES if platform == 'windows-x64' else ['application']:
                        archive.writestr(file, b'fixture')
        prepare(platform, self.output)

    def state(self):
        return json.loads(self.state_file.read_text())

    def configure_state(self, **fields):
        state = self.state()
        state.update(fields)
        self.state_file.write_text(json.dumps(state))

    def publish(self, platform, **environment):
        env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                   MOCK_GH_STATE=str(self.state_file), PE_PYTHON=sys.executable)
        env.update(environment)
        return subprocess.run(['bash', str(self.root / 'scripts/publish_release.sh'), platform],
                              env=env, capture_output=True, text=True)

    def test_android_publishes_when_windows_packages_are_missing(self):
        self.packages('android')
        with self.assertRaises(ValueError):
            prepare('windows-x64', self.output)
        result = self.publish('android')
        self.assertEqual(result.returncode, 0, result.stderr)
        state = self.state()
        self.assertEqual(state['assets']['other-platform-existing.zip'], 'untouched')
        self.assertIn('photo-enhancement-android-arm64-v8a.apk', state['assets'])
        self.assertFalse(any(x[:2] == ['release', 'create'] for x in state['events']))

    def test_macos_architectures_do_not_require_each_other(self):
        self.packages('macos-arm64')
        self.assertFalse((self.output / 'photo-enhancement-macos-x86_64.zip').exists())
        result = self.publish('macos-arm64')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_failed_upload_and_retry_preserve_successful_platform(self):
        self.packages('android')
        self.packages('ios')
        self.configure_state(fail_platform='ios')
        self.assertNotEqual(self.publish('ios').returncode, 0)
        result = self.publish('android')
        self.assertEqual(result.returncode, 0, result.stderr)
        android_assets = dict(self.state()['assets'])
        self.configure_state(fail_platform='!')
        result = self.publish('ios')
        self.assertEqual(result.returncode, 0, result.stderr)
        for name, digest in android_assets.items():
            self.assertEqual(self.state()['assets'][name], digest)
        self.assertIn('SHA256SUMS-ios', self.state()['assets'])
        self.assertIn('SHA256SUMS-android', self.state()['assets'])

    def test_parallel_create_conflict_reuses_shared_release(self):
        self.packages('android')
        self.configure_state(exists=False, create_race=True)
        result = self.publish('android')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.state()['draft'])
        self.assertEqual(sum(x[:2] == ['release', 'create'] for x in self.state()['events']), 1)

    def test_branch_release_tag_and_dev_publication_guard(self):
        self.packages('android')
        self.configure_state(exists=False)
        result = self.publish('android', GITHUB_REF_TYPE='branch', GITHUB_REF_NAME='release-0.1.0')
        self.assertEqual(result.returncode, 0, result.stderr)
        create = next(x for x in self.state()['events'] if x[:2] == ['release', 'create'])
        self.assertEqual(create[2], 'release-0.1.0-build-123')
        self.assertIn('--prerelease=true', create)
        self.configure_state(events=[])
        result = self.publish('android', GITHUB_REF_TYPE='branch', GITHUB_REF_NAME='dev/dev-v0.1.0')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.state()['events'], [])

    def test_metadata_mismatch_rejected_before_remote_changes(self):
        self.packages('android')
        result = self.publish('android', GITHUB_SHA='b' * 40)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.state()['events'], [])

    def test_namespaced_checksums_and_incomplete_windows_bundle(self):
        self.packages('android')
        self.packages('windows-x64')
        for platform in ['android', 'windows-x64']:
            for line in (self.output / f'SHA256SUMS-{platform}').read_text().splitlines():
                digest, name = line.split('  ')
                self.assertEqual(hashlib.sha256((self.output / name).read_bytes()).hexdigest(), digest)
        with zipfile.ZipFile(self.output / 'photo-enhancement-windows-x64.zip', 'w') as archive:
            archive.writestr('photo_enhancement.exe', b'incomplete')
        with self.assertRaises(ValueError):
            prepare('windows-x64', self.output)
        # The invalid platform cannot affect a valid platform's package validation.
        prepare('android', self.output)


if __name__ == '__main__':
    unittest.main()
