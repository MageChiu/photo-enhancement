#!/usr/bin/env python3
"""Check all platform packages before publishing; record provenance and SHA256."""
import hashlib
import json
import os
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parent.parent
output = root / "artifacts"
required = {
    "photo-enhancement-windows-x64.msi",
    "photo-enhancement-windows-x64.zip",
    "photo-enhancement-macos-arm64.zip",
    "photo-enhancement-macos-x86_64.zip",
    "photo-enhancement-macos-arm64.dmg",
    "photo-enhancement-macos-x86_64.dmg",
    "photo-enhancement-android-arm64-v8a.apk",
    "photo-enhancement-android-x86_64.apk",
    "photo-enhancement-ios-arm64-unsigned.zip",
    "photo-enhancement-ios-simulator-arm64.zip",
}
packages = sorted(p for p in output.iterdir() if p.suffix in {".zip", ".apk", ".dmg", ".msi"})
missing = required - {p.name for p in packages}
if missing:
    raise SystemExit(f"Missing packages: {', '.join(sorted(missing))}")
for package in packages:
    if package.stat().st_size == 0:
        raise SystemExit(f"Empty package: {package.name}")
    # DMGs are verified with hdiutil on their macOS build runner.
    if package.suffix == ".dmg":
        continue
    if package.suffix == ".msi":
        with package.open("rb") as source:
            if source.read(8) != bytes.fromhex("d0cf11e0a1b11ae1"):
                raise SystemExit(f"Invalid MSI container: {package.name}")
        continue
    with zipfile.ZipFile(package) as archive:
        if not archive.namelist() or archive.testzip() is not None:
            raise SystemExit(f"Empty or corrupt package: {package.name}")
        if package.name == "photo-enhancement-windows-x64.zip":
            required_files = {
                "photo_enhancement.exe", "photo_core.dll", "flutter_windows.dll",
                "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll",
                "data/icudtl.dat", "data/app.so",
                "data/flutter_assets/assets/presets/builtin.json", "NOTICE.txt",
            }
            absent = required_files - set(archive.namelist())
            if absent:
                raise SystemExit(f"Incomplete Windows application: {', '.join(sorted(absent))}")
metadata = {
    "commit": os.environ["GITHUB_SHA"],
    "ref": os.environ["GITHUB_REF"],
    "run_url": f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}/actions/runs/{os.environ['GITHUB_RUN_ID']}",
    "packages": [p.name for p in packages],
}
manifest = output / "build-info.json"
manifest.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
with (output / "SHA256SUMS").open("w", encoding="utf-8") as checksums:
    for path in packages + [manifest]:
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        checksums.write(f"{digest.hexdigest()}  {path.name}\n")
print(f"Validated {len(packages)} packages; generated build-info.json and SHA256SUMS")
