import 'dart:convert';

const controlNames = <String>[
  'exposure',
  'contrast',
  'highlights',
  'shadows',
  'whites',
  'blacks',
  'temperature',
  'tint',
  'saturation',
  'vibrance',
];
const controlLabels = <String>[
  '曝光',
  '对比度',
  '高光',
  '阴影',
  '白色',
  '黑色',
  '色温',
  '色调',
  '饱和度',
  '自然饱和度',
];

// HSL 8x3, five curve points, split toning 5, sharpening 2, denoise 2.
const defaultEffects = <double>[
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  .25,
  .5,
  .75,
  1,
  0,
  0,
  0,
  0,
  0,
  0,
  1,
  0,
  0,
];

/// Serializable edit recipe only: never contains a native pointer or pixel data.
class EditRecipe {
  EditRecipe({
    List<double>? values,
    List<double>? effects,
    this.lutPath,
    this.lutName,
    this.strength = 1,
    this.lookKind = 0,
    this.lookAdaptive = true,
    this.lookAdaptation = .7,
    this.lookTexture = .35,
    this.lookVignette = .25,
    this.lookGrain = 0,
    this.borderWidth = 0,
    this.borderBottom = 0,
    this.borderColor = 0xffffff,
    this.caption = '',
    this.fontPath = '',
    this.frameLayout = 0,
    this.frameFields = 31,
    this.frameBrand = 0,
    this.logoPath = '',
  })  : values = List.unmodifiable(values ?? List<double>.filled(10, 0)),
        effects = List.unmodifiable(effects ?? defaultEffects);

  final List<double> values;
  final List<double> effects;
  bool get hasEffects {
    for (var i = 0; i < defaultEffects.length; ++i) {
      if (effects[i] != defaultEffects[i]) return true;
    }
    return false;
  }

  final String? lutPath;
  final String? lutName;
  final double strength, borderWidth, borderBottom;
  final int borderColor;
  final String caption, fontPath, logoPath;
  final int frameLayout, frameFields, frameBrand, lookKind;
  final bool lookAdaptive;
  final double lookAdaptation, lookTexture, lookVignette, lookGrain;

  EditRecipe copyWith({
    List<double>? values,
    List<double>? effects,
    String? lutPath,
    String? lutName,
    bool clearLut = false,
    double? strength,
    int? lookKind,
    bool? lookAdaptive,
    double? lookAdaptation,
    double? lookTexture,
    double? lookVignette,
    double? lookGrain,
    double? borderWidth,
    double? borderBottom,
    int? borderColor,
    String? caption,
    String? fontPath,
    int? frameLayout,
    int? frameFields,
    int? frameBrand,
    String? logoPath,
  }) =>
      EditRecipe(
        values: values ?? this.values,
        effects: effects ?? this.effects,
        lutPath: clearLut ? null : lutPath ?? this.lutPath,
        lutName: clearLut ? null : lutName ?? this.lutName,
        strength: strength ?? this.strength,
        lookKind: lookKind ?? this.lookKind,
        lookAdaptive: lookAdaptive ?? this.lookAdaptive,
        lookAdaptation: lookAdaptation ?? this.lookAdaptation,
        lookTexture: lookTexture ?? this.lookTexture,
        lookVignette: lookVignette ?? this.lookVignette,
        lookGrain: lookGrain ?? this.lookGrain,
        borderWidth: borderWidth ?? this.borderWidth,
        borderBottom: borderBottom ?? this.borderBottom,
        borderColor: borderColor ?? this.borderColor,
        caption: caption ?? this.caption,
        fontPath: fontPath ?? this.fontPath,
        frameLayout: frameLayout ?? this.frameLayout,
        frameFields: frameFields ?? this.frameFields,
        frameBrand: frameBrand ?? this.frameBrand,
        logoPath: logoPath ?? this.logoPath,
      );

  EditRecipe withFrame(EditRecipe frame) => copyWith(
      borderWidth: frame.borderWidth,
      borderBottom: frame.borderBottom,
      borderColor: frame.borderColor,
      caption: frame.caption,
      fontPath: frame.fontPath,
      frameLayout: frame.frameLayout,
      frameFields: frame.frameFields,
      frameBrand: frame.frameBrand,
      logoPath: frame.logoPath);

  EditRecipe control(int index, double value) {
    final updated = List<double>.of(values)..[index] = value;
    return copyWith(values: updated);
  }

  Map<String, Object?> toJson() => {
        'version': 1,
        'params': {for (var i = 0; i < 10; ++i) controlNames[i]: values[i]},
        'effects': effects,
        'lutPath': lutPath,
        'lutName': lutName,
        'strength': strength,
        'look': {
          'kind': lookKind,
          'adaptive': lookAdaptive,
          'adaptation': lookAdaptation,
          'texture': lookTexture,
          'vignette': lookVignette,
          'grain': lookGrain
        },
        'border': {
          'width': borderWidth,
          'bottom': borderBottom,
          'color': borderColor,
          'caption': caption,
          'fontPath': fontPath,
          'layout': frameLayout,
          'fields': frameFields,
          'brand': frameBrand,
          'logoPath': logoPath
        },
      };

  factory EditRecipe.fromJson(Map<String, dynamic> json) {
    if (json['version'] != 1) throw const FormatException('不支持的预设版本');
    final params = json['params'];
    if (params is! Map) throw const FormatException('预设缺少参数');
    double number(Object? value, double low, double high,
        [double fallback = 0]) {
      if (value == null) return fallback;
      if (value is! num || !value.isFinite || value < low || value > high) {
        throw const FormatException('预设参数超出范围');
      }
      return value.toDouble();
    }

    String string(Object? value, int max) {
      if (value == null) return '';
      if (value is! String ||
          value.contains('\u0000') ||
          utf8.encode(value).length >= max) {
        throw const FormatException('预设文本过长或格式错误');
      }
      return value;
    }

    final border = json['border'] ?? <String, dynamic>{};
    if (border is! Map) throw const FormatException('边框参数格式错误');
    final color = border['color'] ?? 0xffffff;
    if (color is! int || color < 0 || color > 0xffffff) {
      throw const FormatException('边框颜色无效');
    }
    int option(Object? value, int maximum, int fallback) {
      if (value == null) return fallback;
      if (value is! int || value < 0 || value > maximum) {
        throw const FormatException('边框选项无效');
      }
      return value;
    }

    final path = string(json['lutPath'], 4096);
    final name = string(json['lutName'], 256);
    final effects = json['effects'];
    if (effects != null && (effects is! List || effects.length != 38)) {
      throw const FormatException('高级调色参数格式错误');
    }
    final parsedEffects = <double>[];
    for (var i = 0; i < 38; ++i) {
      final low = i < 24 || i == 33
          ? -1.0
          : i == 35
              ? 1.0
              : 0.0;
      final high = i == 34
          ? 2.0
          : i == 35
              ? 3.0
              : 1.0;
      parsedEffects.add(number(effects == null ? null : (effects as List)[i],
          low, high, defaultEffects[i]));
    }
    final look = json['look'] ?? <String, dynamic>{};
    if (look is! Map ||
        (look['adaptive'] != null && look['adaptive'] is! bool)) {
      throw const FormatException('风格选项无效');
    }
    return EditRecipe(
      effects: parsedEffects,
      values: [
        for (var i = 0; i < 10; ++i)
          number(params[controlNames[i]], i == 0 ? -5 : -1, i == 0 ? 5 : 1)
      ],
      lutPath: path.isEmpty ? null : path,
      lutName: name.isEmpty ? null : name,
      strength: number(json['strength'], 0, 1, 1),
      lookKind: option(look['kind'], 1, 0),
      lookAdaptive: look['adaptive'] as bool? ?? true,
      lookAdaptation: number(look['adaptation'], 0, 1, .7),
      lookTexture: number(look['texture'], 0, 1, .35),
      lookVignette: number(look['vignette'], 0, 1, .25),
      lookGrain: number(look['grain'], 0, 1),
      borderWidth: number(border['width'], 0, .25),
      borderBottom: number(border['bottom'], 0, .4),
      borderColor: color,
      caption: string(border['caption'], 512),
      fontPath: string(border['fontPath'], 1024),
      frameLayout: option(border['layout'], 2, 0),
      frameFields: option(border['fields'], 127,
          string(border['caption'], 512).isEmpty ? 31 : 64),
      frameBrand: option(border['brand'], 4, 0),
      logoPath: string(border['logoPath'], 1024),
    );
  }

  EditRecipe effect(int index, double value) {
    final updated = List<double>.of(effects)..[index] = value;
    return copyWith(effects: updated);
  }

  @override
  bool operator ==(Object other) =>
      other is EditRecipe && jsonEncode(toJson()) == jsonEncode(other.toJson());
  @override
  int get hashCode => jsonEncode(toJson()).hashCode;
}

/// History stores recipes, not full-resolution copies. One drag = one entry.
class EditHistory {
  final List<EditRecipe> _undo = [], _redo = [];
  EditRecipe current = EditRecipe();
  bool get canUndo => _undo.isNotEmpty;
  bool get canRedo => _redo.isNotEmpty;
  void reset([EditRecipe? recipe]) {
    current = recipe ?? EditRecipe();
    _undo.clear();
    _redo.clear();
  }

  void commit(EditRecipe next, {EditRecipe? previous}) {
    final before = previous ?? current;
    if (next == before) return;
    _undo.add(before);
    if (_undo.length > 100) _undo.removeAt(0);
    current = next;
    _redo.clear();
  }

  void undo() {
    if (canUndo) {
      _redo.add(current);
      current = _undo.removeLast();
    }
  }

  void redo() {
    if (canRedo) {
      _undo.add(current);
      current = _redo.removeLast();
    }
  }
}
