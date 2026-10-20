#!/usr/bin/env python3
"""User-run full-resolution decode/export benchmark against a local manifest."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--cli", type=Path, default=Path("build/native/photo_cli"))
    parser.add_argument("--report", type=Path, default=Path("build/sample-report.json"))
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    if manifest.get("version") != 1 or not isinstance(manifest.get("samples"), list):
        parser.error("Expected a v1 sample manifest")
    results = []
    for index, sample in enumerate(manifest["samples"]):
        path = Path(sample["path"])
        start = time.perf_counter()
        with tempfile.TemporaryDirectory(prefix="photo-sample-") as directory:
            target = Path(directory) / f"output-{index}.png"
            process = subprocess.run([str(args.cli.resolve()), str(path), str(target)],
                                     capture_output=True, text=True, timeout=600)
            result = {"path": str(path), "format": sample.get("format"),
                      "camera": sample.get("camera"), "seconds": time.perf_counter() - start,
                      "success": process.returncode == 0,
                      "stdout": process.stdout, "stderr": process.stderr}
            if result["success"]:
                # PNG IHDR dimensions are big-endian at byte 16; no image plugin needed.
                with target.open("rb") as file:
                    header = file.read(24)
                width = int.from_bytes(header[16:20], "big")
                height = int.from_bytes(header[20:24], "big")
                result.update(width=width, height=height)
                if sample.get("expectedWidth") is not None:
                    result["success"] = width == sample["expectedWidth"] and height == sample["expectedHeight"]
            results.append(result)
            print(f"{'PASS' if result['success'] else 'FAIL'} {path}: {result['seconds']:.3f}s")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({"version": 1, "samples": results}, ensure_ascii=False, indent=2))
    raise SystemExit(0 if all(result["success"] for result in results) else 1)


if __name__ == "__main__":
    main()
