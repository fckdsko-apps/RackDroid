#!/usr/bin/env bash
# One-time setup after cloning: fetches the vendored sources.
set -euo pipefail
cd "$(dirname "$0")/../.."

echo "== Initializing Rack + Oboe submodules"
git submodule update --init rack-android/third_party/Rack rack-android/third_party/oboe rack-android/third_party/Fundamental rack-android/third_party/BogaudioModules rack-android/third_party/ValleyRackFree rack-android/third_party/HetrickCV rack-android/third_party/JW-Modules rack-android/third_party/ML_modules rack-android/third_party/RJModules rack-android/third_party/computerscare rack-android/third_party/Little-Utils rack-android/third_party/Autinn rack-android/third_party/FrozenWasteland rack-android/third_party/AudibleInstruments rack-android/third_party/ImpromptuModular rack-android/third_party/CountModula rack-android/third_party/PackOne rack-android/third_party/Bidoo rack-android/third_party/VenomModules rack-android/third_party/GrandeModular rack-android/third_party/sonusmodular rack-android/third_party/AriaModules rack-android/third_party/QuickJS rack-android/third_party/Befaco rack-android/third_party/nonlinearcircuits
git -C rack-android/third_party/HetrickCV submodule update --init --depth 1 Gamma
git -C rack-android/third_party/AudibleInstruments submodule update --init --depth 1 eurorack
git -C rack-android/third_party/AudibleInstruments/eurorack submodule update --init --depth 1 stmlib
git -C rack-android/third_party/PackOne submodule update --init --depth 1
git -C rack-android/third_party/Befaco submodule update --init --depth 1

echo "== Initializing Rack's own dependency submodules (source-built subset)"
cd rack-android/third_party/Rack
git submodule update --init --depth 1 \
	dep/filesystem \
	dep/fuzzysearchdatabase \
	dep/glfw \
	dep/nanovg \
	dep/nanosvg \
	dep/osdialog \
	dep/oui-blendish \
	dep/pffft \
	dep/simde \
	dep/speexdsp \
	dep/tinyexpr

cat <<'EOF'

Done. Next steps:

  Host smoke test (verifies the engine builds outside the official Makefile):
    cmake -S rack-android/native -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build-host
    ./build-host/rack_smoke

  Android app (requires Android Studio or SDK + NDK r26+):
    cd rack-android
    gradle wrapper --gradle-version 8.9   # first time only
    ./gradlew assembleDebug

Remaining deps (jansson, zstd, libarchive) are fetched automatically by CMake
at configure time (FetchContent, pinned tags).
EOF
