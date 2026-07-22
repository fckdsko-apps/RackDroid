---
name: rackdroid-device-testing
description: Use when verifying a RackDroid change on a real Android device over adb — installing, driving the UI, scripting touch gestures (taps, long-press drags, cable drags), and reading the app's logs. Also read before claiming any UI change "works".
version: 0.1.0
---

# Testing RackDroid on a real device

The emulator is not a realistic option for this app (see "Emulator" below), so
verification means a physical arm64 phone over adb.

```sh
export ANDROID_HOME=$HOME/android-sdk
export PATH="$ANDROID_HOME/platform-tools:$PATH"
adb devices -l                     # must show `device`, not `unauthorized`
adb install -r app/build/outputs/apk/release/app-release.apk
adb shell svc power stayon usb     # screen off mid-test wastes a lot of time
adb shell input keyevent KEYCODE_WAKEUP
```

**A signature mismatch means the phone has a build signed with another key.**
`-PdevKeystore` uses the repo's public key; an install from a different machine
(or the standard debug key) will refuse to update and the only way through is
uninstalling, which wipes patches and installed `.rdmod` packs. Ask first.

## Driving the UI

`input tap` and `input swipe` cover most of it, but **`input swipe` cannot
produce a long press**: it starts moving immediately, so `onLongClick` never
fires and anything gated on it (palette tile drag, Rack's right-click menu)
is untestable that way. Use the pointer-level API instead:

```sh
adb shell input motionevent DOWN 318 2040
sleep 1.2                                  # clears the 0.6 s long-press
adb shell input motionevent MOVE 430 1700
adb shell input motionevent UP   430 1350
```

Each command injects one event and returns; the pointer stays down in between,
so a long-press-then-drag is scriptable. This is how cable drags, palette
tile drags and drop-target behaviour get verified.

## Coordinates

- `input` takes **screen pixels**. Native code works in **scene units**:
  `scene = pixels / window->pixelRatio`. On a 1080-wide phone at ~2.6 that
  makes a 46-unit bar about 120 px.
- Screenshots read back at the device's full resolution; if the viewer scales
  them, multiply before using the numbers as tap targets.
- **Coordinates go stale the moment the view changes.** Zoom-to-fit, a pan, or
  a patch reload silently invalidates every hardcoded point, and the taps then
  land on whatever moved there — in this project that has meant dragging random
  modules around mid-test. Re-screenshot and re-derive after any view change,
  and prefer starting each run from a known state.

## Reading what happened

Two sinks, and the difference matters:

- `adb logcat -d -s rackdroid:V rackdroid.modules:V rackdroid.cablepark:V`
- `user/log.txt` inside the app, which the in-app viewer and the
  Documents/RackDroid export can reach — this is the one a USER can send you.

Port-layer files therefore log to both:

```cpp
#define LOGI(...) do { __android_log_print(ANDROID_LOG_INFO, "rackdroid.x", __VA_ARGS__); INFO(__VA_ARGS__); } while (0)
```

A diagnostic that only reaches logcat is invisible to anyone without a cable,
and a silent failure is indistinguishable from a feature that never ran. This
has already cost a debugging round trip on this project.

For crashes: `adb logcat -b crash` and look for `Cause:` plus the first frames
naming `librack_engine.so` — the symbolised function name is usually enough.

## Emulator: don't

Tried and abandoned. It needs 2+ host cores, and without KVM the whole Google
Play image is so slow that `system_server` itself ANRs and dies repeatedly;
app startup took ~20 minutes versus 7 seconds on hardware. Lighter ATD images
boot but carry no `arm64-v8a` translation, so the APK cannot even install.

## What "verified" means here

Say what was actually observed, and what was not. Compiling is not running;
"the API is correct" is not "I saw it work". Several fixes in this project were
handed over as verified when only the build had been checked — including one
that crashed on first launch. If a path could not be exercised (multi-touch
pan, audio quality, anything needing ears), say so plainly rather than letting
the summary imply coverage.

## Related skills

- [[rackdroid-build]] — producing the APK to install.
- [[rackdroid-app-layer]] — the Kotlin/JNI rules a device test tends to expose.
