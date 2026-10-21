#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
case "${1:-}" in
  --help|-h) echo 'Usage: sh scripts/build_android.sh [flutter build apk options]'; exit 0;;
esac
sh scripts/check_toolchains.sh android
cd app
# Gradle invokes root CMake for each ABI and packages libphoto_core.so.
flutter pub get
flutter build apk --release --target-platform android-arm64,android-x64 "$@"
printf '%s\n' 'APK directory: app/build/app/outputs/flutter-apk (relative to project root).'
printf '%s\n' 'Uses the existing development signing configuration. Configure your release keystore before store distribution.'
