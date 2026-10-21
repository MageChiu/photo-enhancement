import 'dart:convert';

/// Content is kept separate from photographic frame settings when switching types.
class FrameTemplate {
  const FrameTemplate(
      {this.category = 'photo',
      this.variant = 0,
      this.filmVariant = 0,
      this.title = 'Photographer',
      this.account = '@photographer',
      this.location = '',
      this.body = '',
      this.timestamp = '',
      this.stats = '',
      this.avatarPath = '',
      this.filmLabel = 'FILM 400',
      this.frameNumber = '01',
      this.holes = true,
      this.showAvatar = true,
      this.showActions = true});
  final String category,
      title,
      account,
      location,
      body,
      timestamp,
      stats,
      avatarPath,
      filmLabel,
      frameNumber;
  final int variant, filmVariant;
  final bool holes, showAvatar, showActions;
  bool get decorated => category == 'film' || category == 'social';
  FrameTemplate copyWith(
          {String? category,
          int? variant,
          int? filmVariant,
          String? title,
          String? account,
          String? location,
          String? body,
          String? timestamp,
          String? stats,
          String? avatarPath,
          String? filmLabel,
          String? frameNumber,
          bool? holes,
          bool? showAvatar,
          bool? showActions}) =>
      FrameTemplate(
          category: category ?? this.category,
          variant: variant ?? this.variant,
          filmVariant: filmVariant ?? this.filmVariant,
          title: title ?? this.title,
          account: account ?? this.account,
          location: location ?? this.location,
          body: body ?? this.body,
          timestamp: timestamp ?? this.timestamp,
          stats: stats ?? this.stats,
          avatarPath: avatarPath ?? this.avatarPath,
          filmLabel: filmLabel ?? this.filmLabel,
          frameNumber: frameNumber ?? this.frameNumber,
          holes: holes ?? this.holes,
          showAvatar: showAvatar ?? this.showAvatar,
          showActions: showActions ?? this.showActions);
  Map<String, Object?> toJson() => {
        'category': category,
        'variant': variant,
        'filmVariant': filmVariant,
        'title': title,
        'account': account,
        'location': location,
        'body': body,
        'timestamp': timestamp,
        'stats': stats,
        'avatarPath': avatarPath,
        'filmLabel': filmLabel,
        'frameNumber': frameNumber,
        'holes': holes,
        'showAvatar': showAvatar,
        'showActions': showActions
      };
  factory FrameTemplate.fromJson(Object? raw) {
    if (raw == null) return const FrameTemplate();
    if (raw is! Map) throw const FormatException('相框模板格式错误');
    String text(String key, int bytes, [String fallback = '']) {
      final value = raw[key] ?? fallback;
      if (value is! String ||
          value.contains('\u0000') ||
          utf8.encode(value).length >= bytes) {
        throw const FormatException('相框文本过长或格式错误');
      }
      return value;
    }

    bool flag(String key) {
      final value = raw[key] ?? true;
      if (value is! bool) throw const FormatException('相框开关格式错误');
      return value;
    }

    final category = text('category', 16, 'photo');
    final variant = raw['variant'] ?? 0;
    final filmVariant = raw['filmVariant'] ?? 0;
    if (!['plain', 'photo', 'film', 'social'].contains(category) ||
        variant is! int ||
        variant < 0 ||
        variant > 3 ||
        filmVariant is! int ||
        filmVariant < 0 ||
        filmVariant > 1) {
      throw const FormatException('相框类型无效');
    }
    return FrameTemplate(
        category: category,
        variant: variant,
        filmVariant: filmVariant,
        title: text('title', 128, 'Photographer'),
        account: text('account', 128, '@photographer'),
        location: text('location', 256),
        body: text('body', 1024),
        timestamp: text('timestamp', 256),
        stats: text('stats', 256),
        avatarPath: text('avatarPath', 1024),
        filmLabel: text('filmLabel', 128, 'FILM 400'),
        frameNumber: text('frameNumber', 128, '01'),
        holes: flag('holes'),
        showAvatar: flag('showAvatar'),
        showActions: flag('showActions'));
  }
}
