#!/usr/bin/env python3
"""Deterministic original SDR sRGB looks. AFP photos are references, not assets.

Colour and tone live in the LUTs. Hanjun adds optional bounded adaptation and
spatial effects in the native pipeline; one strength blends the whole style.
"""
import colorsys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STYLES = ('hanjun', 'on_scene', 'hard_sun', 'blue_hour', 'colour_story',
          'documentary', 'cool_street', 'warm_story')


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


def main():
    target = ROOT / 'app/assets/luts'
    target.mkdir(parents=True, exist_ok=True)
    for name in STYLES:
        size = 33 if name in STYLES[:5] else 17
        with (target / f'{name}.cube').open('w') as out:
            out.write(f'TITLE "Photo Enhancement - {name}"\n')
            out.write(f'LUT_3D_SIZE {size}\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n')
            for b in range(size):
                for g in range(size):
                    for r in range(size):
                        values = transform([r / (size - 1), g / (size - 1), b / (size - 1)], name)
                        out.write(' '.join(f'{v:.7f}' for v in values) + '\n')


if __name__ == '__main__':
    main()
