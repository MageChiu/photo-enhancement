#!/usr/bin/env python3
"""Deterministic original SDR sRGB looks. AFP photos are references, not assets.

Colour and tone live in the LUTs. The stylized family adds bounded adaptation
and spatial effects in the existing pipeline; one strength blends the style.
"""
import argparse
import colorsys
import os
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HERITAGE_STYLES = ('classic_negative', 'classic_cinema', 'natural_colour',
                   'classic_chrome', 'silver_mono', 'hard_mono')
STYLES = ('hanjun', 'on_scene', 'hard_sun', 'blue_hour', 'colour_story',
          'documentary', 'cool_street', 'warm_story',
          'cold_steel', 'dark_red', 'ash') + HERITAGE_STYLES


def clamp(value):
    return max(0.0, min(1.0, value))


def curve(value, points):
    position = clamp(value) * (len(points) - 1)
    index = min(len(points) - 2, int(position))
    return points[index] + (points[index + 1] - points[index]) * (position - index)


def transform(rgb, style):
    # Gentle original base looks, retained as a separate family.
    if style in ('documentary', 'cool_street', 'warm_story'):
        lum = sum(v * k for v, k in zip(rgb, (.2126, .7152, .0722)))
        sat = {'documentary': .95, 'cool_street': .91, 'warm_story': .97}[style]
        result = [lum + (v - lum) * sat for v in rgb]
        shadows, mids = lum * (1 - lum) ** 2, lum * (1 - lum)
        if style == 'cool_street':
            result[0] -= .085 * shadows
            result[2] += .13 * shadows
        elif style == 'warm_story':
            result[0] += .055 * mids
            result[2] -= .045 * mids
        else:
            result[0] -= .018 * shadows
            result[2] += .032 * shadows
        return [clamp(v) for v in result]

    h, light, sat = colorsys.rgb_to_hls(*rgb)
    # Broad, feathered skin-hue protection: a colour heuristic, not detection.
    distance = min(abs(h - .075), 1 - abs(h - .075))
    skin = clamp(1 - distance / .085) * clamp(sat / .18) * clamp((1 - sat) / .25)
    skin *= clamp(light / .15) * clamp((1 - light) / .15)
    if style in HERITAGE_STYLES:
        return heritage(rgb, style, h, sat, skin)
    if style in ('cold_steel', 'dark_red', 'ash'):
        return stylized(rgb, style, h, sat, skin)
    if style == 'hanjun':
        lum = sum(v * k for v, k in zip(rgb, (.2126, .7152, .0722)))
        target = curve(lum, (0, .025, .095, .215, .37, .54, .70, .83, .96))
        red = clamp(1 - min(h, 1 - h) / .085) * sat
        amount = .43 + .27 * skin + .22 * red
        # Compress green/yellow colour while keeping dark red accents distinct.
        green = clamp(1 - abs(h - .30) / .17)
        amount *= 1 - .22 * green * (1 - skin)
        chroma = (target / max(.025, lum)) ** .25
        result = [target + (v - lum) * amount * chroma for v in rgb]
        shadows = 4 * target * (1 - target) ** 2
        protect = 1 - .80 * skin
        result[0] -= .036 * shadows * protect
        result[1] += .010 * shadows * protect
        result[2] += .014 * shadows * protect
        result[1] -= .022 * red * (1 - skin)
        return [clamp(v) for v in result]

    points = {
        'on_scene': (0, .225, .505, .79, 1),
        'hard_sun': (0, .15, .47, .82, 1),
        'blue_hour': (0, .18, .44, .73, 1),
        'colour_story': (0, .20, .50, .81, 1),
    }[style]
    toned = [curve(v, points) for v in rgb]
    lum = sum(v * k for v, k in zip(toned, (.2126, .7152, .0722)))
    shadows, lights = lum * (1 - lum) ** 2, lum ** 2 * (1 - lum)
    if style == 'on_scene':
        result = [lum + (v - lum) * (.90 + .08 * skin) for v in toned]
    elif style == 'hard_sun':
        result = [lum + (v - lum) * (1.10 - .10 * skin) for v in toned]
        warmth = (lights * .55 + shadows * .10) * (1 - .75 * skin)
        result[0] += warmth * .5
        result[1] += warmth * .08
        result[2] -= warmth * .6
    elif style == 'blue_hour':
        result = [lum + (v - lum) * .94 for v in toned]
        protect = 1 - .85 * skin
        result[0] += (-.24 * shadows + .18 * lights) * protect
        result[1] += .035 * shadows * protect
        result[2] += (.30 * shadows - .22 * lights) * protect
    else:
        # Selective red/aqua emphasis, quieter yellow/green, protected skin.
        red_distance = min(h, 1 - h)
        red = clamp(1 - red_distance / .085)
        aqua = clamp(1 - abs(h - .52) / .13)
        amount = .87 + (.25 * red + .30 * aqua) * (1 - .85 * skin)
        result = [lum + (v - lum) * amount for v in toned]
        result[0] -= .10 * shadows * (1 - skin)
        result[2] += .12 * shadows * (1 - skin)
    return [clamp(v) for v in result]


def stylized(rgb, style, hue, saturation, skin):
    """Independent trial palettes; no changes to the eight original looks.

    SDR display RGB in/out, matching the LUT domain. Tone luminance first,
    then retain selected chroma rather than grading each RGB channel separately.
    Hue weights are feathered, including across the red hue seam.
    """
    lum = sum(v * k for v, k in zip(rgb, (.2126, .7152, .0722)))
    points = {
        'cold_steel': (0, .065, .17, .29, .43, .58, .73, .87, .985),
        'dark_red': (0, .035, .12, .245, .40, .565, .73, .855, .965),
        'ash': (0, .05, .16, .30, .465, .635, .79, .90, .98),
    }[style]
    target = curve(lum, points)
    shadow = 4 * target * (1 - target) ** 2
    highlight = 4 * target ** 2 * (1 - target)
    protect = 1 - .85 * skin
    if style == 'cold_steel':
        blue = clamp(1 - abs(hue - .60) / .16) * saturation
        amount = .43 + .32 * skin + .15 * blue
        result = [target + (v - lum) * amount for v in rgb]
        # Blue rather than green shadows; brighter tones approach neutral silver.
        result[0] -= .038 * shadow * protect
        result[1] += .005 * shadow * protect
        result[2] += .052 * shadow * protect
    elif style == 'dark_red':
        red = clamp(1 - min(hue, 1 - hue) / .075) * saturation
        gold = clamp(1 - abs(hue - .115) / .065) * saturation
        amount = .20 + .50 * red + .34 * gold + .38 * skin
        result = [target + (v - lum) * amount for v in rgb]
        result[0] += (.012 * shadow + .014 * highlight) * protect
        result[1] -= .025 * red * protect
        result[1] += .008 * highlight * gold * protect
        result[2] -= (.012 * shadow + .025 * highlight * gold) * protect
    else:
        # Nearly monochrome at full strength. Tiny residual original chroma,
        # without the red selection or blue-green cast of the other palettes.
        result = [target + (v - lum) * .025 for v in rgb]
    return [clamp(v) for v in result]


def fit_chroma(target, offsets):
    """Fit chroma into SDR gamut with one scale, avoiding per-channel clipping.

    Keep the direction of colour offsets when a strong colour reaches a black
    or white boundary. This affects only new heritage-inspired trial palettes.
    """
    scale = 1.0
    for value in offsets:
        if value > 0:
            scale = min(scale, (1 - target) / value)
        elif value < 0:
            scale = min(scale, target / -value)
    return [clamp(target + value * scale) for value in offsets]


def heritage(rgb, style, hue, saturation, skin):
    """Original SDR interpretations of published visual characteristics.

    Not vendor profiles, camera calibration, HNCS or a log-to-display transform.
    Keep each recipe independent from the existing eleven LUTs.
    """
    if style in ('silver_mono', 'hard_mono'):
        weights = (.26, .64, .10) if style == 'silver_mono' else (.30, .59, .11)
        lum = sum(v * k for v, k in zip(rgb, weights))
        points = ((0, .065, .185, .335, .50, .66, .80, .91, .99)
                  if style == 'silver_mono' else
                  (0, .025, .105, .26, .46, .675, .85, .955, 1))
        # Exact neutral RGB; full-strength default avoids residual source colour.
        return [curve(lum, points)] * 3

    lum = sum(v * k for v, k in zip(rgb, (.2126, .7152, .0722)))
    points = {
        'classic_negative': (0, .06, .175, .32, .48, .65, .81, .925, .985),
        'classic_cinema': (.01, .07, .18, .325, .49, .655, .805, .905, .975),
        'natural_colour': (0, .12, .25, .38, .505, .635, .76, .88, 1),
        'classic_chrome': (0, .09, .215, .355, .50, .65, .79, .905, .99),
    }[style]
    target = curve(lum, points)
    shadow = 4 * target * (1 - target) ** 2
    highlight = 4 * target ** 2 * (1 - target)
    protect = 1 - .80 * skin
    if style == 'classic_negative':
        green = clamp(1 - abs(hue - .30) / .15) * saturation
        amount = .82 + .12 * skin - .14 * green
        offsets = [(v - lum) * amount for v in rgb]
        # Cyan-green shadows and magenta highlights: colour contrast rather
        # than a uniform green wash. Skin hues receive weaker split toning.
        offsets[0] += (-.038 * shadow + .028 * highlight) * protect
        offsets[1] += (.020 * shadow - .018 * highlight) * protect
        offsets[2] += (.018 * shadow + .010 * highlight) * protect
        offsets[2] += .025 * green * protect
    elif style == 'classic_cinema':
        offsets = [(v - lum) * (.70 + .22 * skin) for v in rgb]
        offsets[0] += (.025 * shadow + .025 * highlight) * protect
        offsets[1] += .008 * highlight * protect
        offsets[2] -= (.020 * shadow + .030 * highlight) * protect
    elif style == 'classic_chrome':
        magenta = clamp(1 - min(abs(hue - .85), 1 - abs(hue - .85)) / .14)
        amount = .73 + .20 * skin - .10 * magenta * saturation
        offsets = [(v - lum) * amount for v in rgb]
        offsets[0] -= .018 * shadow * protect
        offsets[1] += .003 * shadow * protect
        offsets[2] += .025 * shadow * protect
    else:
        # Natural colour: close to neutral, with gentle tonal shaping and
        # restrained high-saturation chroma. No artificial signature colour cast.
        amount = .98 + .02 * skin - .08 * clamp((saturation - .65) / .35) * (1 - skin)
        offsets = [(v - lum) * amount for v in rgb]
    return fit_chroma(target, offsets)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--styles', nargs='+', choices=STYLES,
                        help='Generate only named styles; leave other LUT files untouched')
    args = parser.parse_args()
    target = ROOT / 'app/assets/luts'
    target.mkdir(parents=True, exist_ok=True)
    for name in dict.fromkeys(args.styles or STYLES):
        size = 17 if name in ('documentary', 'cool_street', 'warm_story') else 33
        destination = target / f'{name}.cube'
        temporary = None
        try:
            # Readers must see either the previous complete LUT or the new one,
            # never a truncated file while bootstrap regenerates its resources.
            with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='\n',
                    dir=target.parent, prefix=f'.{name}-', suffix='.tmp', delete=False) as out:
                temporary = Path(out.name)
                out.write(f'TITLE "Photo Enhancement - {name}"\n')
                out.write(f'LUT_3D_SIZE {size}\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n')
                for b in range(size):
                    for g in range(size):
                        for r in range(size):
                            values = transform([r / (size - 1), g / (size - 1), b / (size - 1)], name)
                            out.write(' '.join(f'{v:.7f}' for v in values) + '\n')
                out.flush()
                os.fsync(out.fileno())
            if destination.exists() and destination.read_bytes() == temporary.read_bytes():
                continue
            temporary.chmod(0o644)
            os.replace(temporary, destination)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
