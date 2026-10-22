#!/usr/bin/env python3
"""Read-only validation of the built-in manifest and its complete .cube files."""
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def validate(path):
    raw = path.read_bytes()
    if not raw or len(raw) > 16 * 1024 * 1024:
        raise ValueError(f'{path.name}: empty or oversized LUT')
    size, rows = None, 0
    domain = {}
    for number, raw_line in enumerate(raw.splitlines(), 1):
        location = f'{path.name}:{number}'
        if b'\0' in raw_line:
            raise ValueError(f'{location}: NUL bytes; resource is damaged')
        line = raw_line.decode('utf-8').split('#', 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        key = fields[0]
        if key == 'TITLE':
            continue
        if key == 'LUT_3D_SIZE':
            if size is not None or rows or len(fields) != 2:
                raise ValueError(f'{location}: invalid/repeated LUT size')
            size = int(fields[1])
            if not 2 <= size <= 65:
                raise ValueError(f'{location}: unsupported LUT size')
            continue
        if key in ('DOMAIN_MIN', 'DOMAIN_MAX'):
            if key in domain or rows or len(fields) != 4:
                raise ValueError(f'{location}: invalid/repeated domain')
            values = list(map(float, fields[1:]))
            if not all(math.isfinite(value) for value in values):
                raise ValueError(f'{location}: non-finite domain')
            domain[key] = values
            continue
        if size is None or len(fields) != 3:
            raise ValueError(f'{location}: expected three RGB values after LUT_3D_SIZE')
        try:
            values = list(map(float, fields))
        except ValueError as error:
            raise ValueError(f'{location}: invalid RGB values/directive') from error
        if not all(math.isfinite(value) and 0 <= value <= 1 for value in values):
            raise ValueError(f'{location}: invalid or out-of-range RGB value')
        rows += 1
    if size is None or rows != size ** 3:
        raise ValueError(f'{path.name}: incomplete LUT ({rows} rows, expected {size ** 3 if size else "size header"})')
    low, high = domain.get('DOMAIN_MIN', [0] * 3), domain.get('DOMAIN_MAX', [1] * 3)
    if any(a >= b for a, b in zip(low, high)):
        raise ValueError(f'{path.name}: invalid domain range')


def main():
    manifest = json.loads((ROOT / 'app/assets/presets/builtin.json').read_text())
    identifiers, filenames = set(), set()
    for spec in manifest:
        name, identifier = spec['lut'], spec['id']
        if name != Path(name).name or '\\' in name or not name.endswith('.cube'):
            raise ValueError(f'Invalid bundled LUT filename: {name!r}')
        if identifier in identifiers or name in filenames:
            raise ValueError(f'Duplicate preset ID or LUT: {identifier!r}, {name!r}')
        identifiers.add(identifier)
        filenames.add(name)
        validate(ROOT / 'app/assets/luts' / name)
    print(f'Validated {len(filenames)} built-in LUT resources.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError) as error:
        print(f'LUT validation failed: {error}', file=sys.stderr)
        sys.exit(1)
