---
name: rackdroid-build
description: Use when building the RackDroid Android app (APK), when a build fails, or when the user asks to "compila", "build the app", "assembleDebug/assembleRelease", or mentions gradle/NDK/CMake errors in this repo.
version: 0.1.0
---

# Building RackDroid

RackDroid is a Gradle project at the repo root (single `:app` module) that
compiles a native C++ engine (Rack v2.6.4 + ~23 plugins) via CMake/NDK on
every build, then packages a Kotlin/NativeActivity Android app around it.
There is no submodule init step — `third_party/` is vendored directly in git.

## Toolchain

```sh
export JAVA_HOME=/path/to/jdk21          # e.g. /usr/lib/jvm/java-21-openjdk-amd64
export ANDROID_HOME=$HOME/android-sdk
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/27.2.12479018"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"
```

Required versions (pinned in
`app/build.gradle.kts` / `gradle/wrapper/gradle-wrapper.properties`):
Gradle 8.9 (wrapper, self-downloading), AGP 8.5.2, Kotlin 2.0.20,
`compileSdk`/`targetSdk` 35, `minSdk` 29, NDK **27.2.12479018** exactly
(flexible-page-size flag needs r27+), CMake 3.22.1, JDK 17 target
(JDK 21 as JAVA_HOME is fine, it compiles down to 17).

## Build commands

From the repo root:

```sh
./gradlew assembleDebug                # fast-ish, unsigned, for device testing
./gradlew assembleRelease -PdevKeystore # signed with the public dev keystore
```

- `-PdevKeystore` forces the checked-in `keystore/rackdroid.keystore`
  (password `rackdroid`, for sideload update-continuity only). Omitting it
  falls back to `~/rackdroid-keystore.properties` if present (private/Play
  key), otherwise also the dev keystore.
- Output APK: `app/build/outputs/apk/release/app-release.apk` (or `debug/`).
- The default build only compiles CMake targets `rackdroid`,
  `plugin_fundamental`, `plugin_drums` (see `app/build.gradle.kts`
  `externalNativeBuild.cmake.targets`) — the lean ~40 MB base APK. The other
  21 plugins still need to be compiled for `.rdmod` packaging; see
  [[rackdroid-add-plugin]] / `scripts/make_rdmods.sh`.

## Build time

On a low-core machine the native build is slow: it compiles ~330
translation units (Rack engine + dep libs + 3 default plugin targets) plus
`FetchContent`-clones jansson/zstd/libarchive/libsamplerate the first time
(needs network). Expect a clean `assembleRelease` to take a long time
(tens of minutes) on a first run with few cores; on such a machine launch it
in the background (`nohup ./gradlew ... > build.log 2>&1 &`) and tail the log
rather than blocking a foreground shell. Subsequent builds are much faster
(Gradle daemon + CMake incremental + Gradle build cache).

## Common failure modes

- **"Rack sources not found in .../third_party/Rack"** (native/CMakeLists.txt
  fatal error) — should never happen on a normal clone (vendored, not a
  submodule); if it does, the clone is corrupted/truncated.
- **FetchContent git-clone failures** — a clean CMake configure needs outbound network
  access to github.com for jansson/zstd/libarchive/libsamplerate.
  If offline, the configure step fails outright.
- **Stale patched-copy sources after touching a vendored plugin** — several
  plugins (Valley, RJModules, Bidoo, Aria) are patched via a "copy to build
  dir, patch the copy, regenerate only `if(NOT EXISTS)`" pattern in
  `native/CMakeLists.txt`. If you edit `third_party/<Plugin>/src/*` directly,
  **delete the CMake build dir** (`app/.cxx/` and/or your standalone
  `build-host/`) so the patched copy regenerates from the new source.
- **16 KB page-size / alignment errors** — only NDK 27+ with
  `-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` (already set) produces
  Play-compliant `.so`s; don't downgrade the NDK version.

## Related skills

- [[rackdroid-host-smoke]] — fast native-only iteration without a full
  Android build (no NDK/Gradle round-trip).
- [[rackdroid-add-plugin]] — adding a new bundled or side-loadable VCV plugin
  pack.
