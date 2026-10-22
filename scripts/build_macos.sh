#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
verify=0
launch=0
for option in "$@"; do
  case "$option" in
    --verify) verify=1 ;;
    --open) launch=1 ;;
    --help|-h)
      printf '%s\n' 'Usage: sh scripts/build_macos.sh [--verify] [--open]' \
        'Builds dist/光屿.app and dist/光屿-macOS.zip.' \
        '--verify: run native tests, Flutter analysis and Flutter tests first.' \
        '--open: launch the standalone application after packaging.'
      exit 0 ;;
    *) printf 'Unknown option: %s\n' "$option" >&2; exit 2 ;;
  esac
done
sh scripts/bootstrap_app.sh
if [ "$verify" -eq 1 ]; then build_tests=ON; else build_tests=OFF; fi
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release \
  -DPE_BUILD_TESTS="$build_tests" -DPE_BUILD_CLI=OFF
cmake --build build/native --parallel 8
if [ "$verify" -eq 1 ]; then
  ctest --test-dir build/native --output-on-failure
  (cd app && flutter analyze && PHOTO_CORE_LIBRARY="$(pwd)/../build/native/libphoto_core.dylib" flutter test)
fi
(cd app && flutter build macos --release)
python3 scripts/bundle_macos.py \
  app/build/macos/Build/Products/Release/photo_enhancement.app \
  --output dist/光屿.app --archive dist/光屿-macOS.zip --display-name 光屿
printf '\n%s\n' '独立 macOS 应用：dist/光屿.app' \
  '压缩包：dist/光屿-macOS.zip' \
  '双击应用即可运行，也可将其拖入“应用程序”。无需 flutter run。' \
  'Finder 定位：open -R "dist/光屿.app"' \
  '直接启动：open "dist/光屿.app"'
if [ "$launch" -eq 1 ]; then
  open "dist/光屿.app"
fi
