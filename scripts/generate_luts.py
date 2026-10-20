#!/usr/bin/env python3
"""Generate reproducible, original SDR sRGB documentary starter LUTs."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def clamp(value):
    return max(0.0, min(1.0, value))


def transform(rgb, style):
    luminance = sum(v * k for v, k in zip(rgb, (0.2126, 0.7152, 0.0722)))
    saturation = {"documentary": 0.95, "cool_street": 0.91, "warm_story": 0.97}[style]
    result = [luminance + (v - luminance) * saturation for v in rgb]
    # Endpoints stay neutral: colored shadows are weighted away from pure black.
    shadows = luminance * (1 - luminance) ** 2
    mids = luminance * (1 - luminance)
    if style == "cool_street":
        result[0] -= 0.085 * shadows
        result[2] += 0.13 * shadows
    elif style == "warm_story":
        result[0] += 0.055 * mids
        result[2] -= 0.045 * mids
    else:
        result[0] -= 0.018 * shadows
        result[2] += 0.032 * shadows
    return [clamp(v) for v in result]


def main():
    target = ROOT / "app/assets/luts"
    target.mkdir(parents=True, exist_ok=True)
    for name in ("documentary", "cool_street", "warm_story"):
        size = 17
        with (target / f"{name}.cube").open("w") as out:
            out.write(f'TITLE "Photo Enhancement - {name}"\n')
            out.write(f"LUT_3D_SIZE {size}\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n")
            for b in range(size):
                for g in range(size):
                    for r in range(size):
                        values = transform([r / (size - 1), g / (size - 1), b / (size - 1)], name)
                        out.write(" ".join(f"{v:.7f}" for v in values) + "\n")


if __name__ == "__main__":
    main()
