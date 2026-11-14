#!/usr/bin/env bash
# Build LYNX SHARE for Android (AGPL-3.0).
# Requirements:
#   - Android NDK r25+ (set ANDROID_NDK_HOME or ANDROID_NDK_ROOT)
#   - CMake 3.22+, Ninja
#   - JDK 17+ (for Gradle wrapper)
#   - JUCE 8.0.4 (fetched by CMake)
#
# Usage:
#   ./scripts/build-android.sh [Debug|Release] [arm64-v8a|armeabi-v7a|x86|x86_64] [install|bundle]
#   Default: Release arm64-v8a bundle
#
# Output:
#   build-android/LYNX_artefacts/<config>/LYNX-<config>.apk  (install)
#   build-android/LYNX_artefacts/<config>/LYNX-<config>.aab  (bundle)

set -euo pipefail

CONFIG="${1:-Release}"
ABI="${2:-arm64-v8a}"
ACTION="${3:-bundle}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build-android"
ARTEFACTS_DIR="$BUILD_DIR/LYNX_artefacts/$CONFIG"

# Check NDK
if [[ -z "${ANDROID_NDK_HOME:-}" && -z "${ANDROID_NDK_ROOT:-}" ]]; then
    echo "ERROR: ANDROID_NDK_HOME or ANDROID_NDK_ROOT must be set" >&2
    exit 1
fi
NDK="${ANDROID_NDK_HOME:-$ANDROID_NDK_ROOT}"

# Check CMake
if ! command -v cmake >/dev/null 2>&1; then
    echo "ERROR: cmake not found" >&2
    exit 1
fi

# Check Ninja
if ! command -v ninja >/dev/null 2>&1; then
    echo "ERROR: ninja not found" >&2
    exit 1
fi

echo "Building LYNX SHARE for Android"
echo "  Config:  $CONFIG"
echo "  ABI:     $ABI"
echo "  Action:  $ACTION"
echo "  NDK:     $NDK"
echo "  Build:   $BUILD_DIR"

# Configure
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM=android-26 \
    -DLYNX_BUILD_APP=ON \
    -DLYNX_BUILD_PLUGINS=OFF \
    -DLYNX_LTO=OFF \
    -DLYNX_PATCH_JUCE=ON

# Build
cmake --build "$BUILD_DIR" --target LYNX -- -j"$(nproc)"

# Package (JUCE's CMake adds the Android packaging targets)
if [[ "$ACTION" == "install" ]]; then
    cmake --build "$BUILD_DIR" --target LYNX_install
    APK="$ARTEFACTS_DIR/LYNX-$CONFIG.apk"
    echo "APK: $APK"
    if [[ -f "$APK" ]]; then
        echo "Install with: adb install -r \"$APK\""
    fi
else
    cmake --build "$BUILD_DIR" --target LYNX_bundle
    AAB="$ARTEFACTS_DIR/LYNX-$CONFIG.aab"
    echo "AAB: $AAB"
    if [[ -f "$AAB" ]]; then
        echo "Upload to Play Console or install with bundletool"
    fi
fi

# Also copy the APK/AAB to dist/ for CI
mkdir -p "$ROOT_DIR/dist"
if [[ "$ACTION" == "install" ]]; then
    cp -f "$ARTEFACTS_DIR/LYNX-$CONFIG.apk" "$ROOT_DIR/dist/LYNX-$CONFIG-android-$ABI.apk" 2>/dev/null || true
else
    cp -f "$ARTEFACTS_DIR/LYNX-$CONFIG.aab" "$ROOT_DIR/dist/LYNX-$CONFIG-android-$ABI.aab" 2>/dev/null || true
fi

echo "Done."