#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
sh scripts/bootstrap_app.sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 8
PHOTO_CORE_LIBRARY="$(pwd)/build/native/libphoto_core.dylib"
export PHOTO_CORE_LIBRARY
cd app
flutter run -d macos "$@"
