#!/bin/sh
# Read-only prerequisite checks. Does not install tools or build applications.
set -u
cd "$(dirname "$0")/.."
target=${1:-all}
missing=0
need_command() {
  if command -v "$1" >/dev/null 2>&1; then
    printf 'OK   %s\n' "$1"
  else
    printf 'MISS %s\n' "$1" >&2
    missing=1
  fi
}
need_file() {
  if [ -f "$1" ]; then printf 'OK   %s\n' "$1";
  else printf 'MISS %s\n' "$1" >&2; missing=1; fi
}
case "$target" in macos|ios|android|ohos|all) ;; *) echo 'Usage: sh scripts/check_toolchains.sh [macos|ios|android|ohos|all]' >&2; exit 2;; esac
need_command cmake
need_command python3
need_command git
need_file third_party/LibRaw/libraw/libraw.h
if [ "$target" != ohos ]; then need_command flutter; fi
if [ "$target" = macos ] || [ "$target" = ios ] || [ "$target" = all ]; then
  need_command xcodebuild
  need_command xcrun
fi
if [ "$target" = macos ] || [ "$target" = all ]; then
  need_command pkg-config
  if command -v pkg-config >/dev/null 2>&1; then
    for package in libjpeg libpng libtiff-4 lcms2 freetype2; do
      if pkg-config --exists "$package"; then printf 'OK   %s\n' "$package";
      else printf 'MISS Homebrew dependency: %s\n' "$package" >&2; missing=1; fi
    done
  fi
fi
if [ "$target" = ios ] || [ "$target" = all ]; then
  echo 'NOTE Install the iOS platform and Simulator runtime in Xcode > Settings > Components.'
fi
if [ "$target" = android ] || [ "$target" = all ]; then
  need_command java
  task_android_sdk=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}
  # setup-android uses a versioned cmdline-tools directory, not always latest/.
  if [ -f "$task_android_sdk/cmdline-tools/latest/bin/sdkmanager" ]; then
    need_file "$task_android_sdk/cmdline-tools/latest/bin/sdkmanager"
  else
    need_command sdkmanager
  fi
  need_file "$task_android_sdk/platforms/android-36/android.jar"
  need_file "$task_android_sdk/build-tools/36.0.0/aapt2"
  need_file "$task_android_sdk/cmake/3.22.1/bin/cmake"
  need_file "$task_android_sdk/ndk/28.2.13676358/build/cmake/android.toolchain.cmake"
  echo 'NOTE Use JDK 17 or Android Studio bundled JBR; run flutter doctor -v and accept Android SDK licenses.'
fi
if [ "$target" = ohos ] || [ "$target" = all ]; then
  if [ -n "${OHOS_FLUTTER:-}" ]; then need_command "$OHOS_FLUTTER";
  else echo 'MISS OHOS_FLUTTER: absolute path to the ohos Flutter executable' >&2; missing=1; fi
  if [ -n "${OHOS_SDK_NATIVE:-}" ]; then need_file "$OHOS_SDK_NATIVE/build/cmake/ohos.toolchain.cmake";
  else echo 'MISS OHOS_SDK_NATIVE: OpenHarmony native SDK directory' >&2; missing=1; fi
  if [ -z "${DEVECO_SDK_HOME:-}" ]; then echo 'MISS DEVECO_SDK_HOME' >&2; missing=1; fi
  need_command ohpm
  need_command hvigorw
  need_command node
  need_command java
fi
if [ "$missing" -ne 0 ]; then echo 'Install missing items using docs/构建环境与命令.md.' >&2; exit 1; fi
echo 'Prerequisite files found. Actual compiler/platform compatibility is checked by the build.'
