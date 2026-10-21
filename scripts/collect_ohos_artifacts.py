#!/usr/bin/env python3
"""Collect only release HAPs, rejecting missing or ambiguous runner outputs."""
from pathlib import Path
import shutil

root = Path(__file__).resolve().parent.parent
output = root / "artifacts"
output.mkdir(exist_ok=True)
haps = sorted((root / "app/ohos/entry/build/default/outputs/default").glob("*.hap"))
if not haps:
    raise SystemExit("No release HAP found in app/ohos/entry/build/default/outputs/default")
for hap in haps:
    shutil.copy2(hap, output / f"photo-enhancement-ohos-arm64-v8a-{hap.name}")
