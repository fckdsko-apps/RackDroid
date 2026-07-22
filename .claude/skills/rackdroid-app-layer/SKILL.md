---
name: rackdroid-app-layer
description: Use when working on RackDroid's Kotlin/Android app layer (MainActivity, module browser/palette, plugin install UI, themes, MIDI/BLE, JNI bridges) rather than the native DSP engine — "MainActivity.kt", "ModuleInstaller", "JNI bridge", "add a toolbar button", "BLE MIDI", ".rdmod install UI".
version: 0.1.0
---

# Kotlin app layer (`app/src/main/java/org/rackdroid/`)

8 files, ~4200 lines total, package `org.rackdroid`. `MainActivity` is a
`NativeActivity` subclass — nearly everything Java/Kotlin-side is glue
between Android APIs and the native engine, not app logic of its own.

| File | Responsibility |
|---|---|
| `MainActivity.kt` (1787) | The hub: permissions, clipboard, blocking dialogs (mirroring `osdialog`), USB MIDI (`MidiManager`) + BLE-MIDI scanning, toolbar, ~20 `external fun` JNI declarations. |
| `ModulePalette.kt` (942) | **The only module picker.** Bottom bar of category chips (ALL / tag categories / MISC) opening a tile strip, plus a separate search window pinned to the TOP of the screen (text + brand chips, two rows of results) — top because the soft keyboard owns the bottom half. Tap or long-press-drag a tile to place. |
| `HelpUi.kt` (849) | In-app help/tutorial sheets (`GuideSheet`, `GuideTopicSheet`, `TutorialLibrarySheet`, `Wizard`). |
| `ModuleInstaller.kt` (189) | `.rdmod` side-load manager — see "Plugin install flow" below. |
| `PianoKeyboardView.kt` (163) | Multi-touch on-screen keyboard, feeds Rack's built-in "Computer keyboard/mouse" driver via `keyboard_native.cpp`. |
| `AppTheme.kt` (126) | Kotlin-**chrome** colour roles only (toolbar/sheets — NOT rack panel art, that's `graphics/`). 4 presets, persisted to SharedPreferences + `rack-theme.txt` (read by `asset_extract.cpp`). |
| `RackService.kt` (60) | Foreground service keeping the engine/Oboe stream alive in the background. |
| `ModuleThumbnails.kt` (58) | `AppFont` + `ThumbnailCache`. The cache looks in `filesDir/thumbnails/` (bundled plugins) then falls back to `filesDir/user/plugins/<slug>/thumbs/` — every non-bundled plugin ships its tile art inside its own `.rdmod`. |

There is **no full-screen module browser** any more; `showNativeBrowser()`
(called from `browser_native.cpp` when Rack opens its own browser) raises the
palette on its ALL category instead.

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

## The two rules that bite hardest

**A JNI entry point must not touch `APP`.** Rack's context is per-thread --
`contextSet`'s own docs say "you must set the context when preparing each
thread" -- and Java's UI thread never had one set, so `APP` is NULL there.
`APP->scene` inside a `native*` function is a null dereference that takes the
whole app down on the first tap. JNI functions may only touch plain state;
anything needing the context belongs on the render thread, e.g. in a widget's
`step()`/`draw()`. This has already caused one crash (cable park toggle).

**Never swallow an exception without logging it.** `runCatching { }` with no
`onFailure`, and `?: return` on a null `listFiles()`, are the shape of every
silent bug found in this app so far: side-loaded packs that never loaded,
exported logs that leaked a file per pause, a startup that deadlocked on its
own dialog. At the last count there were ~46 `runCatching` calls and one
`onFailure`. When adding one, log the failure -- via `jlog` in MainActivity,
which also writes `user/java-log.txt` where a user can actually find it.

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
- [[rackdroid-device-testing]] — verifying the result on hardware.
- [[rackdroid-graphics]] — theme asset trees consumed by `AppTheme.kt`.
