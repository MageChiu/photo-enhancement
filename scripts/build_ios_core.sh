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
  cmake --build "$output" --target photo_core --parallel 8
  # Static archive dependencies are not folded into libphoto_core.a by CMake.
  # Use an explicit archive list: no test/example binaries or host libraries.
  xcrun libtool -static -o "$output/libphoto_core_combined.a" \
    "$output/libphoto_core.a" "$output/libpe_raw.a" \
    "$output/_deps/pe_jpeg-build/libjpeg.a" \
    "$output/_deps/pe_png-build/libpng16.a" \
    "$output/_deps/pe_tiff-build/libtiff/libtiff.a" \
    "$output/_deps/pe_zlib-build/libz.a" \
    "$output/libpe_lcms2.a" "$output/_deps/pe_freetype-build/libfreetype.a" \
    "$output/_deps/pe_webp-build/libwebp.a" \
    "$output/_deps/pe_webp-build/libwebpdemux.a" \
    "$output/_deps/pe_webp-build/libsharpyuv.a" \
    "$output/_deps/pe_heif-build/libheif/libheif.a" \
    "$output/_deps/pe_de265-build/libde265/libde265.a"
done
mkdir -p app/ios/PhotoCore/iphoneos app/ios/PhotoCore/iphonesimulator
cp build/ios-static/iphoneos-arm64/libphoto_core_combined.a app/ios/PhotoCore/iphoneos/libphoto_core.a
xcrun lipo -create build/ios-static/iphonesimulator-arm64/libphoto_core_combined.a \
  build/ios-static/iphonesimulator-x86_64/libphoto_core_combined.a \
  -output app/ios/PhotoCore/iphonesimulator/libphoto_core.a
printf '%s\n' 'iOS native archives ready. Run flutter build ios or flutter build ios --simulator from app/.'
