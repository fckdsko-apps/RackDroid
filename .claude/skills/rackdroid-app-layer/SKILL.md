---
name: rackdroid-app-layer
description: Use when working on RackDroid's Kotlin/Android app layer (MainActivity, module browser/palette, plugin install UI, themes, MIDI/BLE, JNI bridges) rather than the native DSP engine — "MainActivity.kt", "ModuleInstaller", "JNI bridge", "add a toolbar button", "BLE MIDI", ".rdmod install UI".
version: 0.1.0
---

# Kotlin app layer (`app/src/main/java/org/rackdroid/`)

9 files, ~4100 lines total, package `org.rackdroid`. `MainActivity` is a
`NativeActivity` subclass — nearly everything Java/Kotlin-side is glue
between Android APIs and the native engine, not app logic of its own.

| File | Responsibility |
|---|---|
| `MainActivity.kt` (1709 lines) | The hub: permissions, clipboard, blocking dialogs (message/text-prompt/file-picker, mirroring `osdialog`), USB MIDI (`MidiManager`) + BLE-MIDI scanning (`BluetoothLeScanner`/GATT MIDI service UUID), toolbar UI, ~20 `external fun` JNI declarations into `jni_bridge.cpp`/`menu_native.cpp`/`browser_native.cpp`/`keyboard_native.cpp`. |
| `AppTheme.kt` (126) | Kotlin-**chrome** color roles only (toolbar/sheets — NOT rack/module panel art, that's `graphics/`'s job). 4 presets, persisted to SharedPreferences + `rack-theme.txt` (read by native `asset_extract.cpp` to pick which `themes/<name>/` asset tree to apply). |
| `HelpUi.kt` (849) | In-app help/tutorial sheets (`GuideSheet`, `GuideTopicSheet`, `TutorialLibrarySheet`, `Wizard`). |
| `ModuleBrowserSheet.kt` (519) | Native module-add browser: receives a one-shot JSON snapshot from `browser_native.cpp`, does client-side search, loads `.webp` thumbnails (see [[rackdroid-graphics]]). |
| `ModuleInstaller.kt` (174) | `.rdmod` side-load manager — see "Plugin install flow" below. |
| `ModulePalette.kt` (487) | On-canvas quick-add module palette/dock (category chips). |
| `PianoKeyboardView.kt` (163) | Multi-touch on-screen keyboard, feeds Rack's built-in "Computer keyboard/mouse" driver via `keyboard_native.cpp`. |
| `RackService.kt` (60) | Foreground service keeping the engine/Oboe stream alive in the background. |

## Plugin install flow (`ModuleInstaller.kt`) — non-obvious constraint

Android forbids `dlopen()` of a `.so` directly from shared/external storage
on API 24+ (linker namespaces). The flow is therefore:

1. Watch `Android/data/org.rackdroid/files/Modules/` for new `.rdmod` zips.
2. Unzip into app-private storage: `filesDir/user/plugins/<slug>/`.
3. `System.load()` the `.so` from private storage in Kotlin (the one path
   that satisfies the app classloader's linker namespace).
4. Call `nativeLoadUserPlugin(dir, soname)` (JNI, declared in
   `MainActivity.kt`) → native side does `dlopen(RTLD_NOLOAD)` to pick up
   the already-loaded library and register it (`static_plugins.cpp`).

Zip-slip is guarded on extraction. **Uninstalling a pack cannot unload the
native library mid-session** — requires an app restart; don't try to "fix"
this without changing the native loader's lifecycle too.

This sideload path is explicitly **not Play-policy-compliant** (native code
must not execute from outside Play on the Play channel) — a Play build needs
Play Asset Packs instead, reusing the same install-into-private-storage +
`System.load` loader. Don't extend the sideload mechanism as if it were also
the Play distribution path.

## JNI bridge conventions

`MainActivity.kt` declares `external fun` for every native call; matching
implementations live in `native/port/jni_bridge.cpp` (clipboard/dialogs),
`menu_native.cpp` (bottom-sheet menus), `browser_native.cpp` (module
browser), `keyboard_native.cpp` (on-screen keyboard passthrough). When
adding a new Kotlin↔native call, follow the existing pairing: declare the
`external fun` in `MainActivity.kt`, implement the JNI entry point in the
matching `native/port/*.cpp` file (not a new one, unless it's a genuinely
new subsystem), and keep the blocking/synchronous contract consistent with
the sibling calls in that file (e.g. dialogs block the calling thread and
must keep pumping the native glue looper — see `jni_bridge.cpp`'s comment on
this — don't introduce a non-blocking call into a file whose siblings are
all blocking without checking callers can handle that).

## MIDI/BLE minSdk constraint

Bluetooth LE MIDI scanning branches on API level in
`MainActivity.showBleMidiScanner()`: `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT` on
API 31+, `ACCESS_FINE_LOCATION` + legacy `BLUETOOTH`/`BLUETOOTH_ADMIN` on API
29-30. `minSdk = 29` is set by the native AMidi API floor, not by this
branch — don't raise `minSdk` casually, it has a specific reason recorded in
`PORTING.md`'s "Debiti tecnici correnti" section.

## Related skills

- [[rackdroid-build]] — compiling the app after Kotlin/JNI changes.
- [[rackdroid-graphics]] — theme asset trees consumed by `AppTheme.kt`.
