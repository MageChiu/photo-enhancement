#!/usr/bin/env python3
"""Package selected raster masters using macOS sips; no third-party libraries.

--ohos-only installs the pre-sized icon in a generated OHOS host, without sips.
No build, SDK download, or application launch is performed.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
MASTERS = ROOT / "design/app-icon"


def resize(source, destination, size):
    destination.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["sips", "--resampleHeightWidth", str(size), str(size),
         str(source), "--out", str(destination)],
        check=True, stdout=subprocess.DEVNULL,
    )


def apple_icons(platform, master):
    folder = ROOT / "app" / platform / "Runner/Assets.xcassets/AppIcon.appiconset"
    entries = json.loads((folder / "Contents.json").read_text())["images"]
    generated = set()
    for entry in entries:
        filename = entry.get("filename")
        if not filename or filename in generated:
            continue
        width, height = map(float, entry["size"].split("x"))
        scale = float(entry["scale"].removesuffix("x"))
        if width != height or not (width * scale).is_integer():
            raise ValueError(f"Invalid square icon specification: {entry}")
        resize(master, folder / filename, int(width * scale))
        generated.add(filename)
    print(f"{platform}: updated {len(generated)} app icon PNGs")


def ohos_icons():
    host = ROOT / "app/ohos"
    if not host.exists():
        print("OHOS: master prepared; build_ohos.sh installs it after host generation")
        return
    # Change only existing application/ability icon fields in generated hosts.
    # Keep unrelated media, splash graphics and other JSON5 configuration.
    pattern = re.compile(r'''(?P<prefix>(?:["']icon["']|\bicon)\s*:\s*)(?P<quote>["'])\$media:[^"']+(?P=quote)''')
    configs = [
        (host / "AppScope/app.json5", host / "AppScope/resources/base/media"),
        (host / "entry/src/main/module.json5", host / "entry/src/main/resources/base/media"),
    ]
    count = 0
    for config, media in configs:
        if not config.is_file():
            continue
        original = config.read_text()
        updated, matches = pattern.subn(
            lambda m: m["prefix"] + m["quote"] + "$media:photo_app_icon" + m["quote"], original,
        )
        if not matches:
            continue
        media.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / "platform/ohos/app_icon.png", media / "photo_app_icon.png")
        if updated != original:
            config.write_text(updated)
        count += matches
    if not count:
        raise RuntimeError("OHOS host has no recognizable application/ability icon fields")
    print(f"OHOS: connected {count} application/ability icon fields")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ohos-only", action="store_true")
    args = parser.parse_args()
    if not args.ohos_only:
        if not shutil.which("sips"):
            parser.error("Regenerating PNG sizes requires macOS sips; generated assets are committed")
        apple_icons("macos", MASTERS / "viewfinder-macos.png")
        apple_icons("ios", MASTERS / "viewfinder-square.png")
        for density, size in [("mdpi", 48), ("hdpi", 72), ("xhdpi", 96),
                              ("xxhdpi", 144), ("xxxhdpi", 192)]:
            resize(MASTERS / "viewfinder-square.png",
                   ROOT / f"app/android/app/src/main/res/mipmap-{density}/ic_launcher.png", size)
        print("android: updated 5 launcher icon PNGs")
        resize(MASTERS / "viewfinder-square.png", ROOT / "platform/ohos/app_icon.png", 1024)
    ohos_icons()


if __name__ == "__main__":
    main()
