#!/usr/bin/env python3
"""Reject an incomplete Windows application directory before packaging it."""
import ctypes
import os
from pathlib import Path
import struct
import sys


def verify(bundle):
    required = [
        'photo_enhancement.exe', 'photo_core.dll', 'flutter_windows.dll',
        'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
        'data/icudtl.dat', 'data/app.so',
        'data/flutter_assets/assets/presets/builtin.json',
    ]
    for name in required:
        path = bundle / name
        if not path.is_file() or not path.stat().st_size:
            raise RuntimeError(f'Missing or empty application file: {name}')
    # Verify actual x64 PE binaries rather than relying on artifact names.
    for name in ['photo_enhancement.exe', 'photo_core.dll', 'flutter_windows.dll']:
        with (bundle / name).open('rb') as source:
            if source.read(2) != b'MZ':
                raise RuntimeError(f'Not a Windows binary: {name}')
            source.seek(0x3c)
            offset = struct.unpack('<I', source.read(4))[0]
            source.seek(offset)
            if source.read(6) != b'PE\0\0\x64\x86':
                raise RuntimeError(f'Not an x64 PE binary: {name}')
    # Search only the DLL's directory and Windows system libraries. Do not let
    # an installed developer SDK or the vcpkg PATH hide missing packaged codecs.
    flags = 0x100 | 0x800  # LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | ..._SYSTEM32
    core = ctypes.CDLL(str(bundle / 'photo_core.dll'), winmode=flags)
    ctypes.CDLL(str(bundle / 'flutter_windows.dll'), winmode=flags)
    core.pe_abi_version.restype = ctypes.c_uint32
    if core.pe_abi_version() != 1:
        raise RuntimeError('Unexpected image core ABI')
    for symbol in ['pe_process_file_template', 'pe_read_lens_info', 'pe_image_free']:
        getattr(core, symbol)
    core.pe_capabilities.restype = ctypes.c_char_p
    capabilities = core.pe_capabilities().decode('utf-8').split(',')
    missing = {'jpeg', 'png', 'tiff', 'raw', 'webp', 'heif'} - set(capabilities)
    if missing:
        raise RuntimeError(f'Missing packaged codecs: {sorted(missing)}')
    heif_dlls = [bundle / name for name in ['heif.dll', 'libheif.dll'] if (bundle / name).is_file()]
    if len(heif_dlls) != 1:
        raise RuntimeError('Expected exactly one packaged libheif DLL')
    heif = ctypes.CDLL(str(heif_dlls[0]), winmode=flags)
    heif.heif_have_decoder_for_format.argtypes = [ctypes.c_int]
    heif.heif_have_decoder_for_format.restype = ctypes.c_int
    if not heif.heif_have_decoder_for_format(1):  # heif_compression_HEVC
        raise RuntimeError('Packaged libheif cannot decode HEIC/HEVC')
    print('Windows x64 DLL loading and application directory verified')


if __name__ == '__main__':
    if os.name != 'nt':
        raise SystemExit('Run this packaging check on Windows')
    verify(Path(sys.argv[1]).resolve())
