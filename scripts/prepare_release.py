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
    "photo-enhancement-macos-arm64.zip",
    "photo-enhancement-macos-x86_64.zip",
    "photo-enhancement-android-arm64-v8a.apk",
    "photo-enhancement-android-x86_64.apk",
    "photo-enhancement-ios-arm64-unsigned.zip",
    "photo-enhancement-ios-simulator-arm64.zip",
}
packages = sorted(p for p in output.iterdir() if p.suffix in {".zip", ".apk", ".hap"})
missing = required - {p.name for p in packages}
if missing:
    raise SystemExit(f"Missing packages: {', '.join(sorted(missing))}")
if not any(p.name.startswith("photo-enhancement-ohos-arm64-v8a-") and p.suffix == ".hap" for p in packages):
    raise SystemExit("Missing HarmonyOS release HAP")
for package in packages:
    with zipfile.ZipFile(package) as archive:
        if not archive.namelist() or archive.testzip() is not None:
            raise SystemExit(f"Empty or corrupt package: {package.name}")
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
