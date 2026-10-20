#!/bin/sh
# Use an OpenHarmony Flutter SDK explicitly; official Flutter cannot build HAPs.
set -eu
cd "$(dirname "$0")/.."
case "${1:-}" in
  --help|-h) echo 'Usage: sh scripts/build_ohos.sh [--prepare-only]'; exit 0;;
  --prepare-only|'') ;;
  *) echo "Unknown option: $1" >&2; exit 2;;
esac
sh scripts/check_toolchains.sh ohos
: "${OHOS_FLUTTER:?Set OHOS_FLUTTER to the ohos Flutter executable}"
: "${OHOS_SDK_NATIVE:?Set OHOS_SDK_NATIVE to the SDK native directory}"
toolchain="$OHOS_SDK_NATIVE/build/cmake/ohos.toolchain.cmake"
test -f "$toolchain" || { echo "Missing toolchain: $toolchain" >&2; exit 1; }
created=0
if [ ! -d app/ohos ]; then
  "$OHOS_FLUTTER" create --no-pub --platforms=ohos --org=local.photo \
    --project-name=photo_enhancement build/ohos_runner_template
  cp -R build/ohos_runner_template/ohos app/ohos
  created=1
fi
ability=app/ohos/entry/src/main/ets/entryability/EntryAbility.ets
if [ "$created" -eq 1 ]; then
  cp platform/ohos/EntryAbility.ets "$ability"
elif ! grep -q 'local.photo/files' "$ability"; then
  echo 'Existing EntryAbility needs the file bridge. Merge platform/ohos/EntryAbility.ets into it before building; no host files were overwritten.' >&2
  exit 1
fi
python3 scripts/generate_app_icons.py --ohos-only
if [ "${1:-}" = --prepare-only ]; then
  echo 'Open app/ohos in DevEco Studio, select matching SDK and configure signing; then rerun without --prepare-only.'
  exit 0
fi
cmake -S . -B build/ohos/arm64-v8a -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DOHOS_ARCH=arm64-v8a -DOHOS_STL=c++_shared -DCMAKE_BUILD_TYPE=Release \
  -DPE_PORTABLE_DEPS=ON -DPE_BUILD_TESTS=OFF -DPE_BUILD_CLI=OFF
cmake --build build/ohos/arm64-v8a --target photo_core --parallel 8
mkdir -p app/ohos/entry/libs/arm64-v8a
cp build/ohos/arm64-v8a/libphoto_core.so app/ohos/entry/libs/arm64-v8a/
# Flutter packages libc++_shared with its engine; do not mix a host runtime.
(cd app && "$OHOS_FLUTTER" pub get && "$OHOS_FLUTTER" build hap --release)
