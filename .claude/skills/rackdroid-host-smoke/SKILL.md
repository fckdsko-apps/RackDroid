---
name: rackdroid-host-smoke
description: Use when iterating on the native C++ Rack engine/port code (native/, third_party/ plugin sources) and you want a fast headless or GLES3 desktop check without a full Android/Gradle/NDK build — "host smoke test", "rack_smoke", "rack_ui_smoke", "test the engine on Linux".
version: 0.1.0
---

# Host smoke tests (fast native iteration)

`native/CMakeLists.txt` builds standalone Linux executables directly with
`cmake` (no Gradle/NDK involved) to verify the DSP engine and (optionally)
the full nanovg/GLES3 UI stack compile and run correctly on the desktop
before touching the much slower Android build. There is no wrapper script —
invoke CMake directly.

## Headless engine check (`rack_smoke`)

Verifies the engine subset compiles outside the Android toolchain and runs
1 second of simulated audio, then shuts down cleanly. No graphics deps.

```sh
cmake -S native -B build-host
cmake --build build-host -j1        # 1 core on this machine — do not raise -j
./build-host/rack_smoke
```

## Full UI smoke test (`rack_ui_smoke`)

Builds the complete nanovg/GLES3 UI stack plus all 23 plugin targets
(everything `add_dependencies(rack_ui_smoke ...)` lists) against Mesa's
EGL surfaceless platform — the same crash surface the real app would hit,
runnable without a device/emulator.

Requires `libegl1-mesa-dev` and `libgles-dev` (`apt-get install` if missing —
not installed on this machine as of the last check, install before first use).

```sh
cmake -S native -B build-host-ui -DRACKDROID_HOST_UI=ON
cmake --build build-host-ui -j1
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 ./build-host-ui/rack_ui_smoke [frames] [mode] [outdir]
```

CLI args (positional, no `--help`):
- `argv[1]` — frame count to run (default 60).
- `argv[2]`:
  - `--menu` — menu interaction test mode.
  - `--probe` — prints engine frame count per pass.
  - `--all-modules` — instantiates and renders **every** registered model
    across all plugins (the regression check mentioned in `PORTING.md`:
    `rack_ui_smoke N --all-modules`). Run this after adding/porting a plugin.
  - `--export-thumbnails [outdir]` — renders one PNG per model to `outdir`
    (default `thumbnails/`). Feeds the module-browser tile art pipeline —
    after exporting, run `python3 graphics/webp_thumbs.py` to convert the
    PNGs to `.webp` and commit under `graphics/browser-thumbs/` (see
    [[rackdroid-graphics]]).

## When to use which

- Changed engine/DSP/port code, no UI/rendering concerns → `rack_smoke`.
- Changed/added a plugin's module set, panel layout, or anything
  UI-adjacent → `rack_ui_smoke ... --all-modules` first (catches
  registration/rendering crashes cheaply), then a real Android build only
  once this passes.
- **Always** re-run `--all-modules` after adding a new plugin target in
  `native/CMakeLists.txt` (see [[rackdroid-add-plugin]]) before wiring it
  into the Gradle/Android build.

## Gotcha

If you previously configured a build dir before this skill existed, reusing
it across a plain build and a `-DRACKDROID_HOST_UI=ON` build (or vice versa)
mixes stale CMake cache state — use separate build dirs (`build-host` vs
`build-host-ui`) as shown above rather than toggling the option in place.
