# RackDroid — Modular Synthesizer for Android

**Unofficial** port of the [VCV Rack 2](https://vcvrack.com) engine to Android,
built on unmodified upstream sources (`third_party/Rack`, pinned to v2.6.4) plus
a native porting layer and a touch-friendly interface. Not affiliated with VCV.

Build modular patches on your phone: connect oscillators, filters, envelopes,
sequencers and more with virtual cables, just like a hardware rack.

> **Version 0.1.0** · repo: [`nowheel/RackDroid`](https://github.com/nowheel/RackDroid)

## What it does

- **Native low-latency audio engine** (Oboe/AAudio), full-duplex.
- **66 built-in modules** always included: **Core** (Audio/MIDI), **Fundamental**
  (39: VCO, VCF, VCA, ADSR, LFO, SEQ-3, Delay, Mixer, Scope, Quantizer…) and
  **RackDroid Drums** (14 original drum voices in the 808 tradition).
- **Touch interface**: drag to create cables and move modules, pinch to zoom,
  long-press a knob to type a value.
- **Glass toolbar** with menus (File/Edit/View/Engine/Help) and tools: module
  palette, plugin manager, undo/redo, layout/parameter lock, MIDI, keyboard,
  recording, info. Collapses into a tab.
- **Module palette**: chips by category (VCO, LFO, VCF, VCA, ENV, SEQ, DRUM,
  MIX, FX, NOISE, QNT, MIDI, UTIL), draggable previews onto the rack, tap to
  insert at center, **ⓘ** badge showing each module's name/description/tags.
  Swipe down to close.
- **Bottom-sheet menus** with swipe-down to dismiss; **voltage** and **cable
  opacity** sliders in the View menu.
- **On-screen musical keyboard** (with octave shift and ✕ close) and **Bluetooth
  LE MIDI**.
- **Recording** of the output to a **WAV** file in `Documents/RackDroid/`.
- **30 step-by-step tutorials** across 5 levels, plus a topic-based guide.

## Additional modules (.rdmod)

Beyond the built-in modules, you can add packages (Bogaudio, Valley, Audible,
Impromptu, Befaco, HetrickCV…) **on the fly**, without updating the app:

- **From the app**: *Module Manager* tool → *Install from file* → pick one or
  more `.rdmod` files. They are loaded immediately; uninstall them from the same
  manager.
- **From a folder**: copy the `.rdmod` files to
  `Android/data/org.rackdroid/files/Modules/` and restart.

Package format, native loading mechanism, and instructions for **creating** a
plugin: see **[MODULES.md](MODULES.md)** and the manual at
**[docs/rackdroid-manuale.pdf](docs/rackdroid-manuale.pdf)**.

## Build

Gradle project at the repo root (arm64-v8a, `minSdk 33`). All `third_party/`
sources (Rack v2.6.4, Oboe, all plugins) are **vendored in the repo**: a clean
clone compiles as-is, no submodule init needed.

```sh
export JAVA_HOME=~/jdk21; export ANDROID_HOME=~/android-sdk
gradle assembleRelease -PdevKeystore    # gradle 8.7+, or open in Android Studio
```

- `-PdevKeystore` signs with the public development key (update continuity for
  sideloading; use a private key for Play).
- The base APK is ~40 MB: it contains only the built-in modules; CMake still
  compiles all plugins, but non-base `.so` files are excluded from the APK and
  distributed as `.rdmod` (`packaging.jniLibs.excludes`, see
  `scripts/make_rdmods.sh`).

## Structure

```
app/            Android module (Gradle, manifest, MainActivity + Kotlin UI)
native/
  CMakeLists.txt  Rack engine + dependencies + port layer build
  port/           porting code (Oboe audio, menus, browser, plugin loader…)
  host/           engine/UI smoke tests on Linux
drums/          RackDroid Drums (first-party package, original code + panels)
graphics/       original graphics (panels, thumbnails, rebuilt ComponentLibrary)
third_party/    Rack, Oboe and plugin sources (upstream untouched)
scripts/        setup.sh (sources) · make_rdmods.sh (packages the .rdmod files)
docs/           user manual (PDF + HTML source)
MODULES.md      .rdmod format, loading mechanism and plugin creation
```

Guiding principle: **zero patches to Rack's sources**. All platform-specific
code lives in `native/port/`; desktop-only files are excluded from the build and
replaced, so upgrading to new upstream versions remains a simple submodule bump.

## Licenses, trademarks and distribution — important

- Rack's code is **GPLv3**: this port is GPLv3 and the complete sources are in
  the repository (license obligation satisfied ✓).
- **Trademark**: the app presents itself as "RackDroid" (custom icon, rebranded
  strings); the "VCV" name/logo is not used ✓.
- **Graphics**: the original ComponentLibrary and Core panels are **CC BY-NC-ND
  4.0** (non-commercial). RackDroid uses **rebuilt** graphics (`graphics/`,
  GPLv3) in their place to be distributable; Fundamental/Bogaudio etc. plugins
  are GPLv3 with graphics included ✓.
- **Signing**: `keystore/rackdroid.keystore` is a **development** key with a
  public password (`rackdroid`) — for update continuity when sideloading, NOT
  for authenticity. For a store, generate a private key (or use Play App
  Signing).
- **Google Play**: distributing native code executed from **outside** Play
  violates their policies; the `.rdmod` folder / file installation are for
  sideload/GitHub builds. For Play, deliver extra packages via *asset packs*.

---

RackDroid is a port of VCV Rack (GPLv3). Not affiliated with or endorsed by VCV.
