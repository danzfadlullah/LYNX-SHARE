# Building LYNX SHARE for Android

LYNX SHARE can be built as an Android app (APK / AAB) using JUCE 8's Android support. The core (`src/core`, `src/flp`) is plain C++20 and compiles for Android unchanged; the platform layer (`src/platform/`) has Android implementations for paths, real-time audio (no-op, the OS manages it), native plugin embedding, and video decoding (MediaCodec via JNI).

## Prerequisites

- **Android NDK r25 or newer** (r26+ recommended). Set `ANDROID_NDK_HOME` or `ANDROID_NDK_ROOT` to the NDK root.
- **CMake 3.22+** and **Ninja**.
- **JDK 17+** (for Gradle wrapper that JUCE invokes).
- **Linux, macOS, or Windows (WSL2)** as the build host.
- JUCE 8.0.4 is fetched automatically by CMake (`FetchContent`).

## Quick Start

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r26b
./scripts/build-android.sh Release arm64-v8a bundle
```

Outputs:
- `build-android/LYNX_artefacts/Release/LYNX-Release.aab` — Android App Bundle (for Play Console)
- `build-android/LYNX_artefacts/Release/LYNX-Release.apk` — APK (for sideloading, `./scripts/build-android.sh ... install`)

## Supported ABIs

| ABI | Status | Notes |
|-----|--------|-------|
| `arm64-v8a` | ✅ Primary | 64-bit ARM (most devices) |
| `armeabi-v7a` | ⚠️ Works | 32-bit ARM (older devices); JUCE's ARM NEON paths work |
| `x86_64` | ⚠️ Works | Emulators / Chrome OS |
| `x86` | ❌ Not tested | 32-bit x86 (rare) |

Build for multiple ABIs by running the script multiple times; Gradle merges them into one AAB.

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `LYNX_BUILD_APP` | `ON` | Build the app (required for Android) |
| `LYNX_BUILD_PLUGINS` | `OFF` | Build VST3/CLAP plugins (not applicable to Android) |
| `LYNX_LTO` | `ON` | Link-time optimisation (disabled for Android in the script for faster links) |
| `LYNX_PATCH_JUCE` | `ON` | Apply LYNX's JUCE patches (Linux drag-and-drop fixes; harmless on Android) |

Android-specific `juce_add_gui_app` properties (in `CMakeLists.txt`):
- `ANDROID_PACKAGE` — `com.ka1s.lynx`
- `ANDROID_VERSION_CODE` — `127` (matches 0.1.27)
- `ANDROID_VERSION_NAME` — `0.1.27`
- `ANDROID_MIN_SDK` — `26` (Android 8.0 Oreo)
- `ANDROID_TARGET_SDK` — `34` (Android 14)
- `ANDROID_ICON` — `resources/android/res/mipmap-xxxhdpi/ic_launcher.png`

## Platform Layer (What's Different on Android)

| Area | Desktop | Android (`src/platform/Android*`) |
|------|---------|-----------------------------------|
| Settings folder | `%APPDATA%` / `~/Library/Application Support` / `~/.config` | App-private files dir (`Context.getFilesDir()`) |
| Cache folder | `%APPDATA%` / `~/Library/Caches` / `~/.cache` | App-private cache dir (`Context.getCacheDir()`) |
| Shared storage | Documents folder | `/sdcard` (via Storage Access Framework, user-granted) |
| Real-time audio | SCHED_FIFO / RealtimeKit / MMCSS | **No-op** — OS manages AAudio / OpenSL ES thread priority |
| Plugin editor embedding | HWND / NSView / X11 | `ANativeWindow` (SurfaceView) via `juce::AndroidComponentPeer` |
| Video decoding | Media Foundation / AVFoundation / ffmpeg | **MediaCodec** (JNI → `VideoDecoder.java`) |
| Fonts | System fonts / bundled TTF | Bundled Open Sans in APK assets |

## JNI / Java Side

The Android build generates a `JNI_OnLoad` and an `Application` subclass through JUCE's CMake. The Java-side video decoder lives at:

```
app/src/main/java/com/ka1s/lynx/video/VideoDecoder.java
```

This file is **not in the repo** — JUCE's Android template creates a stub. You need to implement `VideoDecoder.java` using `MediaExtractor` + `MediaCodec` (see `src/ui/VideoMedia.cpp` `#elif JUCE_ANDROID` for the C++ JNI signatures it expects).

### Expected `VideoDecoder.java` API

```java
package com.ka1s.lynx.video;

import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.graphics.Bitmap;
import java.nio.ByteBuffer;

public class VideoDecoder {
    public VideoDecoder(String path) { /* MediaExtractor setup */ }
    public void prepare() { /* async, calls onPrepared */ }
    public void seek(double seconds) { /* seek to keyframe */ }
    public void decodeFrame() { /* decode one frame, calls onFrame */ }
    public void seekAudio(double seconds) { /* audio seek */ }
    public void release() { /* release MediaCodec/Extractor */ }

    // Called from C++ via JNI (see VideoMedia.cpp)
    private void onPrepared(int width, int height, double duration, double fps) { /* ... */ }
    private void onFrame(Bitmap bitmap, long timestampUs) { /* ... */ }
    private void onAudio(float[] data, int channels, int frames, long timestampUs) { /* ... */ }
}
```

## Known Limitations (Alpha)

- **Video decoding**: The C++ JNI glue exists (`VideoMedia.cpp` `#elif JUCE_ANDROID`), but `VideoDecoder.java` is a stub. Video tracks and the Video Player will not decode video until it's implemented.
- **Audio I/O**: Uses JUCE's `AudioManager` → AAudio (low-latency) or OpenSL ES fallback. Works for playback; recording needs `RECORD_AUDIO` permission (added to manifest by JUCE when `MICROPHONE_PERMISSION_ENABLED=TRUE`).
- **MIDI**: Android MIDI API not yet wired; USB MIDI devices may work through JUCE's generic MIDI input.
- **Plugin hosting**: VST3 hosting compiles (the VST3 SDK is cross-platform), but most VST3 plugins are x86/ARM Windows/macOS binaries and won't load on Android. CLAP plugins likewise.
- **File access**: Projects must be opened through the Storage Access Framework (SAF) document picker. The app cannot freely scan `/sdcard`.
- **Screen size**: The UI is designed for desktop windows. On phones it will be very cramped; tablets / foldables / DeX are the realistic targets.
- **Performance**: The audio engine is not yet optimised for mobile CPUs (no ARM NEON SIMD paths for the built-in synths). Expect higher CPU usage than on desktop.

## Testing

```bash
# Build APK
./scripts/build-android.sh Debug arm64-v8a install

# Install to connected device/emulator
adb install -r build-android/LYNX_artefacts/Debug/LYNX-Debug.apk

# Run with logcat
adb logcat -s "LYNX" "AudioTrack" "MediaCodec" "JNI"
```

## CI (GitHub Actions)

The `.github/workflows/tests.yml` can be extended with an Android build job:

```yaml
- name: Setup NDK
  uses: android-actions/setup-android@v3
  with:
    ndk-version: r26b

- name: Build Android
  run: |
    export ANDROID_NDK_HOME=$ANDROID_NDK_ROOT
    ./scripts/build-android.sh Release arm64-v8a bundle

- name: Upload AAB
  uses: actions/upload-artifact@v4
  with:
    name: lynx-android-aab
    path: dist/LYNX-Release-android-arm64-v8a.aab
```

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `CMAKE_TOOLCHAIN_FILE` not found | NDK path wrong | Check `ANDROID_NDK_HOME` |
| `JNI_OnLoad` not found | JUCE Android template not applied | Re-run CMake with clean build dir |
| `VideoDecoder` class not found | Java stub not implemented | Add `VideoDecoder.java` to `app/src/main/java/...` |
| Audio crackling / high CPU | No NEON SIMD, debug build | Build `Release`, consider `LYNX_LTO=ON` |
| App crashes on startup | Missing permissions / native lib | Check logcat; `RECORD_AUDIO`, `READ_MEDIA_VIDEO` |

## License

AGPL-3.0 — same as the rest of LYNX SHARE. The Android build includes JUCE (GPL-3.0 with commercial option) and the VST3 SDK (Steinberg VST3 License). Ensure compliance if distributing.