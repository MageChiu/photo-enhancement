#!/bin/sh
# Generate only platform runners. Never replace the hand-written app sources.
set -eu
cd "$(dirname "$0")/.."
command -v flutter >/dev/null 2>&1 || { echo 'Flutter SDK is required (see app/.fvmrc and README).' >&2; exit 1; }
python3 scripts/generate_luts.py
python3 scripts/validate_luts.py
template="build/flutter_runner_template"
if [ ! -d "$template/macos" ]; then
  flutter create --no-pub --platforms=macos,ios,android --org=local.photo --project-name=photo_enhancement "$template"
fi
for platform in macos ios android; do
  if [ ! -d "app/$platform" ]; then cp -R "$template/$platform" "app/$platform"; fi
done
# MVP is a locally distributed app, not a sandboxed Mac App Store build. Library
# loading from the build tree and large RAW access require this during dev.
for file in app/macos/Runner/DebugProfile.entitlements app/macos/Runner/Release.entitlements; do
  /usr/libexec/PlistBuddy -c 'Set :com.apple.security.app-sandbox false' "$file"
done
(cd app && flutter pub get)
