#!/bin/sh
# Build self-contained static archives for device and both simulator hosts.
set -eu
cd "$(dirname "$0")/.."
for target in iphoneos-arm64 iphonesimulator-arm64 iphonesimulator-x86_64; do
  sdk=${target%-*}
  arch=${target##*-}
  output="build/ios-static/$target"
  cmake -S . -B "$output" -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES="$arch" -DCMAKE_SYSTEM_PROCESSOR="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_BUILD_TYPE=Release \
    -DPE_PORTABLE_DEPS=ON -DPE_STATIC_CORE=ON -DPE_WITH_HEIF=ON \
    -DPE_BUILD_TESTS=OFF -DPE_BUILD_CLI=OFF
  cmake --build "$output" --config Release --target photo_core --parallel 8
  # Static archive dependencies are not folded into libphoto_core.a by CMake.
  # CMake emits an explicit target list, excluding tests/examples/host libraries.
  manifest="$output/ios-core-archives-Release.txt"
  test -s "$manifest" || { echo "Missing iOS archive manifest: $manifest" >&2; exit 1; }
  set --
  while IFS= read -r archive; do
    test -f "$archive" || { echo "Missing iOS static archive: $archive" >&2; exit 1; }
    set -- "$@" "$archive"
  done < "$manifest"
  xcrun libtool -static -o "$output/libphoto_core_combined.a" "$@"
done
mkdir -p app/ios/PhotoCore/iphoneos app/ios/PhotoCore/iphonesimulator
cp build/ios-static/iphoneos-arm64/libphoto_core_combined.a app/ios/PhotoCore/iphoneos/libphoto_core.a
xcrun lipo -create build/ios-static/iphonesimulator-arm64/libphoto_core_combined.a \
  build/ios-static/iphonesimulator-x86_64/libphoto_core_combined.a \
  -output app/ios/PhotoCore/iphonesimulator/libphoto_core.a
printf '%s\n' 'iOS native archives ready. Run flutter build ios or flutter build ios --simulator from app/.'
