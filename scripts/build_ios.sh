#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mode=--simulator
if [ "$#" -gt 0 ]; then mode=$1; shift; fi
case "$mode" in
  --simulator|--device|--signed) ;;
  --help|-h) echo 'Usage: sh scripts/build_ios.sh [--simulator|--device|--signed] [flutter build ios options]'; exit 0;;
  *) echo "Unknown iOS mode: $mode" >&2; exit 2;;
esac
sh scripts/check_toolchains.sh ios
python3 scripts/validate_luts.py
sh scripts/build_ios_core.sh
cd app
flutter pub get
case "$mode" in
  --simulator) flutter build ios --simulator "$@" ;;
  --device) flutter build ios --release --no-codesign "$@" ;;
  --signed) flutter build ios --release "$@" ;;
esac
printf '%s\n' 'iOS products: app/build/ios (relative to the project root).'
