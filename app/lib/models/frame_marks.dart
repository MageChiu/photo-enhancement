/// Stable IDs are stored in presets; labels and asset paths can change.
const frameMarks = <String, String>{
  '': '不显示',
  'auto': '根据 EXIF 自动选择',
  'sony': 'SONY',
  'sony-alpha': 'Sony α',
  'hasselblad': 'HASSELBLAD',
  'hasselblad-h': '哈苏 H',
  'fujifilm': 'FUJIFILM',
  'canon': 'Canon',
  'nikon': 'Nikon',
  'leica': 'Leica',
  'sigma': 'SIGMA',
  'panasonic': 'Panasonic',
  'lumix': 'LUMIX',
  'olympus': 'OLYMPUS',
  'om-system': 'OM SYSTEM',
  'pentax': 'PENTAX',
  'ricoh': 'RICOH',
  'dji': 'DJI',
  'gopro': 'GoPro',
  'kodak': 'Kodak',
};
const frameAccents = <String, String>{
  '': '不显示',
  'auto': '根据镜头信息自动选择',
  'sony-g': 'Sony G',
  'sony-gm': 'Sony G Master',
  'canon': 'Canon',
  'canon-l': 'Canon L',
  'nikon': 'Nikon',
  'nikon-s': 'NIKKOR S-Line',
  'sigma': 'SIGMA',
  'sigma-art': 'SIGMA Art',
  'sigma-contemporary': 'SIGMA Contemporary',
  'sigma-sports': 'SIGMA Sports',
  'tamron': 'TAMRON',
  'zeiss': 'ZEISS',
  'fujifilm': 'FUJIFILM 镜头',
  'hasselblad': 'HASSELBLAD XCD / HC',
};
const frameFonts = <String, String>{
  'sans': '简洁无衬线',
  'mono': '等宽参数',
  'serif': '作品衬线',
  'custom': '本地字体',
};

String markForMake(String make, [String model = '']) {
  final name = make.toLowerCase();
  if (name.contains('sony')) return 'sony';
  if (name.contains('hasselblad')) return 'hasselblad';
  if (name.contains('fuji')) return 'fujifilm';
  if (name.contains('canon')) return 'canon';
  if (name.contains('nikon')) return 'nikon';
  if (name.contains('leica')) return 'leica';
  if (name.contains('sigma')) return 'sigma';
  if (name.contains('panasonic') || name.contains('lumix')) return 'lumix';
  if (name.contains('olympus')) return 'olympus';
  if (name.contains('om digital') || name.contains('om system')) {
    return 'om-system';
  }
  if (name.contains('pentax') ||
      (name.contains('ricoh') && model.toLowerCase().contains('pentax'))) {
    return 'pentax';
  }
  if (name.contains('ricoh')) return 'ricoh';
  if (name.contains('dji')) return 'dji';
  if (name.contains('gopro')) return 'gopro';
  if (name.contains('kodak')) return 'kodak';
  return '';
}

/// Match lens fields, never infer a lens brand from the camera make.
String accentForLens(String make, String model) {
  final name =
      '$make $model'.toLowerCase().replaceAll(RegExp(r'\s+'), ' ').trim();
  if (name.isEmpty) return '';
  if (name.contains('sigma') ||
      RegExp(r'\b(?:dg|dc)\s+(?:dn|hsm)\b').hasMatch(name)) {
    if (RegExp(r'\bart\b').hasMatch(name)) return 'sigma-art';
    if (RegExp(r'\bcontemporary\b').hasMatch(name)) return 'sigma-contemporary';
    if (RegExp(r'\bsports\b').hasMatch(name)) return 'sigma-sports';
    return 'sigma';
  }
  if (name.contains('tamron')) return 'tamron';
  if (name.contains('zeiss')) return 'zeiss';
  if (name.contains('sony') || RegExp(r'^fe\s+\d').hasMatch(name)) {
    if (RegExp(r'\bgm\b|\bg master\b').hasMatch(name)) return 'sony-gm';
    if (RegExp(r'\bg\b').hasMatch(name)) return 'sony-g';
    return '';
  }
  if (name.contains('canon') || RegExp(r'^(?:rf|ef)[ -]?\d').hasMatch(name)) {
    return RegExp(r'\bf\s*/?\s*\d+(?:\.\d+)?l\b|\bl\b').hasMatch(name)
        ? 'canon-l'
        : 'canon';
  }
  if (name.contains('nikon') || name.contains('nikkor')) {
    return RegExp(r'\bs\b|s-line').hasMatch(name) ? 'nikon-s' : 'nikon';
  }
  if (name.contains('fujifilm') ||
      name.contains('fujinon') ||
      RegExp(r'^(?:xf|gf)[ -]?\d').hasMatch(name)) {
    return 'fujifilm';
  }
  if (name.contains('hasselblad') || RegExp(r'^xcd\s').hasMatch(name)) {
    return 'hasselblad';
  }
  return '';
}

int resolvedMarkTone(int tone, int background) {
  if (tone != 0) return tone;
  final brightness = .2126 * ((background >> 16) & 255) +
      .7152 * ((background >> 8) & 255) +
      .0722 * (background & 255);
  return brightness > 160 ? 2 : 3;
}

String frameAssetKey(String mark, String accent, int tone) =>
    'mark:$mark:$accent:$tone';
