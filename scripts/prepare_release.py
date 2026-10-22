#!/usr/bin/env python3
"""Validate only one platform's packages and generate its unique metadata."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import zipfile

PLATFORM_PACKAGES = {
    'macos-arm64': ['photo-enhancement-macos-arm64.zip', 'photo-enhancement-macos-arm64.dmg'],
    'macos-x86_64': ['photo-enhancement-macos-x86_64.zip', 'photo-enhancement-macos-x86_64.dmg'],
    'windows-x64': ['photo-enhancement-windows-x64.msi', 'photo-enhancement-windows-x64.zip'],
    'android': ['photo-enhancement-android-arm64-v8a.apk', 'photo-enhancement-android-x86_64.apk'],
    'ios': ['photo-enhancement-ios-arm64-unsigned.zip', 'photo-enhancement-ios-simulator-arm64.zip'],
}


def prepare(platform, output):
    packages = [output / name for name in PLATFORM_PACKAGES[platform]]
    for package in packages:
        if not package.is_file() or package.stat().st_size == 0:
            raise ValueError(f'Missing or empty package: {package.name}')
        # DMGs are verified with hdiutil on their macOS build runner.
        if package.suffix == '.dmg':
            continue
        if package.suffix == '.msi':
            with package.open('rb') as source:
                if source.read(8) != bytes.fromhex('d0cf11e0a1b11ae1'):
                    raise ValueError(f'Invalid MSI container: {package.name}')
            continue
        with zipfile.ZipFile(package) as archive:
            if not archive.namelist() or archive.testzip() is not None:
                raise ValueError(f'Empty or corrupt package: {package.name}')
            if package.name == 'photo-enhancement-windows-x64.zip':
                required_files = {
                    'photo_enhancement.exe', 'photo_core.dll', 'flutter_windows.dll',
                    'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
                    'data/icudtl.dat', 'data/app.so',
                    'data/flutter_assets/assets/presets/builtin.json', 'NOTICE.txt',
                }
                absent = required_files - set(archive.namelist())
                if absent:
                    raise ValueError(f'Incomplete Windows application: {sorted(absent)}')
    metadata = {
        'platform': platform,
        'run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT', '1'),
        'commit': os.environ['GITHUB_SHA'],
        'ref': os.environ['GITHUB_REF'],
        'run_url': f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}/actions/runs/{os.environ['GITHUB_RUN_ID']}",
        'packages': [p.name for p in packages],
    }
    manifest = output / f'build-info-{platform}.json'
    manifest.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    with (output / f'SHA256SUMS-{platform}').open('w', encoding='utf-8') as checksums:
        for path in packages + [manifest]:
            digest = hashlib.sha256()
            with path.open('rb') as source:
                for chunk in iter(lambda: source.read(1024 * 1024), b''):
                    digest.update(chunk)
            checksums.write(f'{digest.hexdigest()}  {path.name}\n')
    print(f'Validated {platform}: {len(packages)} packages, metadata and checksums')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', required=True, choices=PLATFORM_PACKAGES)
    args = parser.parse_args()
    prepare(args.platform, Path(__file__).resolve().parent.parent / 'artifacts')
