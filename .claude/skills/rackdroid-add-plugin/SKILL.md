---
name: rackdroid-add-plugin
description: Use when porting a new VCV Rack plugin package into RackDroid, adding it as a CMake build target, or packaging existing plugins as .rdmod side-load files — "add a plugin", "port a VCV module pack", "make_rdmods", ".rdmod", "add a CMake plugin target".
version: 0.1.0
---

# Adding / packaging a VCV plugin in RackDroid

RackDroid bundles 2 plugins in the base APK (Fundamental, RackDroidDrums) and
ships 21 more as on-demand `.rdmod` side-load packages (see `MODULES.md` for
the runtime loading mechanism). Both paths share the same CMake plugin
target as their build step.

## Guiding principle: zero patches to vendored sources

`third_party/<Plugin>/` is vendored (plain committed directory, **not** a git
submodule — confirmed, no `.gitmodules` anywhere). Never edit files under
`third_party/` in place. When a plugin needs a source fix to compile for
Android/host (keyword collisions, missing includes, license-encumbered files
to strip), follow the existing pattern in `native/CMakeLists.txt`: copy the
plugin's `src/` into `${CMAKE_CURRENT_BINARY_DIR}/<name>_src`, patch the
copy, and guard regeneration with `if(NOT EXISTS ...)`. Examples already in
the file: Valcorrect Valley's `__decay`→`__lpgDecay` clang keyword clash,
RJModules' uninitialized `tsf*` fix, Bidoo/Aria stripping
network-dependent or non-GPL-compatible-license source files.

**Gotcha**: because regeneration is guarded by `if(NOT EXISTS)`, if you ever
touch the *upstream* vendored source under `third_party/` (e.g. bumping a
plugin to a newer version), you must manually delete the stale patched copy
in the CMake build directory or your fix will keep being reused.

## Adding a new CMake plugin target

Most plugins fit the `add_simple_plugin` helper (defined around line 498 of
`native/CMakeLists.txt`):

```cmake
add_simple_plugin(plugin_<name> ${<NAME>_DIR} ${<NAME>_SOURCES})
```

This creates a `SHARED` library named `plugin_<name>`, linked against
`rack_engine`, with `-I<dir>/src -O3 -funsafe-math-optimizations -w`. Look at
any of the ~18 existing `add_simple_plugin` calls (JW-Modules, ML_modules,
computerscare, Autinn, FrozenWasteland, ImpromptuModular, CountModula,
Bidoo, Venom, GrandeModular, sonusmodular, Befaco, nonlinearcircuits,
plugin_drums, ...) as templates.

Plugins needing extra handling are written by hand instead (see Fundamental,
Bogaudio, Valley, HetrickCV, Aria in the same file) for: extra
dependencies/link libs, embedded binary blobs (Valley's ROM `.bin` via
`xxd -i`, run with `WORKING_DIRECTORY` set to the plugin dir so symbol names
match upstream exactly), or patched-copy source fixes.

After adding a target:
1. Build it: `cmake --build build-host -j1` (see [[rackdroid-host-smoke]]) —
   fix compile errors with the patched-copy pattern, never by editing
   `third_party/` directly.
2. Regression-check it renders: `./rack_ui_smoke N --all-modules` (build with
   `-DRACKDROID_HOST_UI=ON` first) — this instantiates every registered
   model, catching plugin-loading/rendering crashes cheaply.
3. If it should ship as a side-loadable `.rdmod` (the common case — only
   Fundamental/Drums are bundled in the base APK), add it to
   `scripts/make_rdmods.sh`'s list of `pack` calls (see below) — do **not**
   add it to `app/build.gradle.kts`'s CMake `targets` list, and **do** add
   its `.so` name to `packaging.jniLibs.excludes` there so it's excluded
   from the base APK.
4. If it should be bundled in the base APK instead (rare — increases the
   ~40 MB base size), add it to `externalNativeBuild.cmake.targets` in
   `app/build.gradle.kts` instead, and do NOT exclude its `.so`.

**Module browser tile art follows the same bundled/`.rdmod` split**: only
Core/Fundamental/RackDroidDrums thumbnails ship in the base APK's
`thumbnails.zip` (`app/build.gradle.kts`'s `packThumbnailAssets` now
`include()`s just those three). Every other plugin's thumbnails
(`graphics/browser-thumbs/<slug>/`) are packed automatically into that
plugin's `.rdmod` as `thumbs/` by `scripts/make_rdmods.sh`'s shared `pack()`
function — nothing extra to wire up per plugin, as long as the thumbnail
dir name matches the plugin's `slug`. `ThumbnailCache.get()`
(`ModuleBrowserSheet.kt`) checks the bundled tree first, then falls back to
the installed pack's own `user/plugins/<slug>/thumbs/` dir.

## Packaging plugins as `.rdmod` (`scripts/make_rdmods.sh`)

Prerequisite: build every plugin target first (the default Gradle build only
compiles `rackdroid`, `plugin_fundamental`, `plugin_drums`):

```sh
ANDROID_HOME=~/android-sdk gradle externalNativeBuildRelease \
  -Pandroid.injected.build.abi=arm64-v8a
# or: drop the `targets` restriction in app/build.gradle.kts temporarily
```

Then, from the repo root, **after** a release build:

```sh
scripts/make_rdmods.sh [OUT_DIR]     # default OUT_DIR=/tmp/rdmods
```

This strips each plugin's `.so` with the NDK's own `llvm-strip` (auto-located
under `$ANDROID_HOME/ndk/*/toolchains/...`) and zips it with `plugin.json`
and `res/` into `<slug>.rdmod`. Producing a new pack means adding a
`pack <slug> <soname> <third_party_dir> <regen_res_dir|->` line — use `-` for
"take res/ as-is from third_party/<dir>", or a `graphics/<name>-res` dir name
for regenerated (non-commercial-license-replacement) art. Trailing
extra-copy-spec arguments handle special files (e.g. RJModules'
rawwaves/soundfonts, Befaco's spring-reverb IR).

**Gotcha**: the script auto-picks the newest
`app/build/intermediates/cxx/RelWithDebInfo/*/obj/arm64-v8a` dir — a bare
rebuild after switching build variants can leave a stale `Debug` dir that
gets picked up by mistake; if packed `.so`s look wrong (e.g. huge/unstripped
or pre-dating a toolchain flag change), delete `app/build/intermediates/cxx`
and rebuild release from scratch.

## Panel art licensing (only relevant if the plugin's original art is CC BY-NC/ND)

If the plugin's stock panel art isn't commercially redistributable, don't
ship it as-is — see [[rackdroid-graphics]] for regenerating replacement SVGs
via `graphics/regen_graphics.py` (the pattern already used for
ComponentLibrary/Core/Fundamental/FrozenWasteland/AudibleInstruments/
ImpromptuModular/CountModula/GrandeModular). Check `graphics/NOTICE-graphics.md`
for the current attribution/licensing status of each bundled/packaged plugin
before assuming its stock art is fine to ship.

## Related skills

- [[rackdroid-host-smoke]] — building/running `rack_smoke`/`rack_ui_smoke`.
- [[rackdroid-build]] — full Android build once the plugin is wired in.
- [[rackdroid-first-party-module]] — writing an all-original module (no
  third-party source to port at all), using `drums/` as the template.
- [[rackdroid-graphics]] — regenerating replacement panel art.
