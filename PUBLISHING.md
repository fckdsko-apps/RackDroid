# Publishing RackDroid

Everything needed to put RackDroid on the Google Play Store (or sell it via
sideload). The app is legally distributable **commercially**: engine/modules
are GPL-3.0 (source published), all VCV CC-BY-NC graphics are replaced with
original artwork, and the only third-party art (Bogaudio, CC BY-SA) is
attributed in-app (ⓘ button) and in NOTICE-graphics.md.

## Build artifacts

```sh
cd rackdroid
export ANDROID_HOME=/path/to/android-sdk
./gradlew assembleSideloadRelease -PdevKeystore  # sideload APK, public development key
./gradlew assembleSideloadRelease -PdevKeystore -PtargetAbis=x86_64 # x86_64 sideload APK
./gradlew bundlePlayRelease -PtargetAbis=arm64-v8a,x86_64   # Play AAB, both 64-bit ABIs
# Expansion packs are ABI-specific and require every optional target:
./gradlew assembleSideloadRelease -PdevKeystore -PallPlugins -PtargetAbis=x86_64
scripts/make_rdmods.sh /tmp/rdmods-x86_64 x86_64 # requires 21 valid archives
```

Play Store accepts only the **AAB**. Sideloading/other stores use the APK.
The current application is version **0.1.2** (`versionCode 3`), targets API 35,
supports Android 10+ (`minSdk 29`), and builds arm64-v8a libraries with 16 KB
page-size support. The optional x86_64 target is intended for emulators and
compatible ChromeOS devices. The 32-bit x86 ABI is not supported.

## Signing key — REQUIRED before publishing

`keystore/rackdroid.keystore` is a development key with a public password; it
only gives update continuity for sideloads. **Before publishing to Play**:

1. Generate a private upload key and keep it secret (or enrol in Play App
   Signing and let Google manage the app-signing key):
   ```sh
   keytool -genkeypair -v -keystore my-upload.keystore -alias upload \
     -keyalg RSA -keysize 2048 -validity 10000
   ```
2. Create `~/rackdroid-keystore.properties` (outside the repository):
  ```properties
  storeFile=/absolute/path/to/my-upload.keystore
  storePassword=...
  keyAlias=upload
  keyPassword=...
  ```
  `app/build.gradle.kts` reads this file automatically. Never commit the
  properties file, private keystore, passwords, `.claude/`, or backup keys.
3. Build `bundlePlayRelease` **without** `-PdevKeystore`, then verify the resulting
  AAB/upload certificate before uploading it.

## GPLv3 compliance (mandatory when selling)

You may sell GPL software, but you must provide the complete corresponding
source to recipients. The ⓘ Credits dialog links to this repository, which
satisfies the written-offer requirement — keep that repo public and in sync
with each released build, or host the exact source alongside the store page.

## Privacy policy (Play requires a URL)

Host the text below and link it in the Play listing. It is accurate for the
current build:

> **RackDroid Privacy Policy**
> RackDroid runs entirely on your device. It does not collect, transmit, or
> share any personal data. It has no analytics, no ads and no accounts. The
> version published on Google Play makes no network requests at all and does
> not hold the INTERNET permission. Audio input (microphone) and MIDI/Bluetooth
> permissions are used only locally, in real time, to process audio and
> connect controllers; nothing is recorded or sent anywhere. Patches you save
> stay in the app's private storage until you export them yourself.
> Contact: patrik.meneguot@gmail.com

The GitHub build additionally offers an opt-in update check; `PRIVACY.md`
covers both distributions and is the text to host if one URL has to serve
them both.

## Store listing (draft)

- **Title**: RackDroid — Modular Synth
- **Short description**: A touch-first modular synthesizer for Android with
  66 built-in modules and installable expansion packs.
- **Full description**:
  > RackDroid brings full modular synthesis to your phone and tablet. Patch
  > oscillators, filters, envelopes, sequencers and effects together with
  > virtual cables and build your own sounds from scratch.
  >
  > • 66 built-in modules: oscillators, filters, envelopes, drums, sequencers,
  >   scopes and utilities
  > • 21 optional expansion packs, including Bogaudio, Valley and Befaco
  > • Low-latency audio engine (Oboe/AAudio)
  > • USB and Bluetooth LE MIDI controllers
  > • Multitouch: drag knobs and cables, pinch to zoom, two-finger pan
  > • Save, open and share your patches
  > • Runs in the background so your patch keeps playing
  >
  > Built on the open-source VCV Rack engine (GPLv3). Not affiliated with VCV.
- **Category**: Music & Audio
- **Content rating**: Everyone
- **Required assets**: 512×512 icon (derive from ic_launcher), feature
  graphic 1024×500, and phone/tablet screenshots (capture from a device).
  Existing repository screenshots are documentation assets; recapture the
  final two-row toolbar before using them in a store listing.

## Device release smoke test

With one authorized Android device attached:

```sh
ANDROID_HOME=/path/to/android-sdk scripts/device_smoke.sh
```

The script installs the development-signed release APK, launches it, checks
that the activity remains resumed and scans the process log for fatal errors.
It also saves a screenshot under `app/build/reports/device-smoke/`.

Manual checks still required on at least one phone:

- grant/deny microphone and notification permissions;
- hear audio from the included template and test background playback;
- open the two-row toolbar, every menu, module palette, search and keyboard;
- with multi-select on: tap a module to pick it, tap again to drop it, hold to
  move the selection, and confirm knobs and ports stay put while the mode is on;
- check a selected module keeps its panel readable under the red halo, zoomed
  in and zoomed out;
- delete a selection, read the count in the dialog, cancel once, then confirm
  and undo it;
- open a module's menu and check Preset ▸ Copy/Paste settings, while the Edit
  menu's own Copy/Paste and a text field's keep their usual labels;
- drag a normal cable near a compatible jack and verify highlight + snap;
- park a cable, pan, reconnect it, and verify cancellation/discard behavior;
- install at least one `.rdmod`, verify its brand/modules/thumbnails appear
  immediately, restart, then uninstall it;
- save, reopen, import and share a `.vcv` patch;
- connect USB MIDI and, where available, Bluetooth LE MIDI;
- start/stop WAV recording and verify the file in `Documents/RackDroid`.

### Recorded device checks

- 2026-07-26 — Samsung SM-S901E, Android 16 / API 36: development-signed
  0.1.2 APK installed, cold start completed, patch rendered, foreground audio
  service active, 372 models published, two-row toolbar rendered and no fatal
  startup exception detected. That count is this device's: it has expansion
  packs side-loaded, where a clean install publishes the 66 built-in modules.
  Audio/MIDI and `.rdmod` interaction checks remain manual and are not implied
  by this automated smoke result.

- 2026-07-26 — Microsoft Surface Go 2, BlissOS 16.9.7, Android 13 / API 33,
  x86_64: development-signed 0.1.2 APK installed over the previous build, cold
  start in 411 ms, patch restored. First-run tour checked in landscape and
  portrait — every spotlight lands on its target. Multi-select verified by
  measuring the frames, not by eye: mode toggle, tap to select, second tap to
  deselect, hold to move the whole selection, undo, delete dialog reporting the
  right count, cancel leaving the patch intact, and the relabelled preset rows.
  x86_64 stays an internal build: audio, MIDI and long sessions are untested
  there, and it is deliberately not announced on the site.

### Recorded build checks

- Linux host smoke loaded Core, both base plugins and all 21 optional plugins,
  then instantiated and rendered 1004 registered models successfully.
- Development-signed arm64 APK and AAB built successfully. The APK/AAB payload
  contains only Fundamental and RackDroid Drums; optional libraries remain out
  of the base application.
- Development-signed x86_64 APK built successfully and its payload contains
  only x86_64 native libraries. Runtime smoke testing still requires an
  x86_64 emulator or device.
- All 21 `.rdmod` archives were generated from the arm64 release build and
  passed ZIP integrity, required-library and required-resource checks.

## Pre-launch checklist

- [x] Android 10+ (`minSdk 29`), target API 35 and 16 KB native page support
- [x] Development-signed release APK and technical-validation AAB build
- [x] All 21 optional `.rdmod` archives build and pass integrity checks
- [x] Privacy policy text maintained in `PRIVACY.md`
- [x] Base APK contains 66 modules; non-base packs are on-demand `.rdmod`
- [ ] Private upload key configured and backed up securely
- [ ] Production AAB generated and signing certificate verified
- [ ] Privacy policy hosted publicly and URL added to the listing
- [ ] Source repository public and tagged at the exact released commit (GPLv3)
- [ ] Smoke test completed on Android 10, one mid-range device and current API
- [ ] Audio latency, background audio, USB MIDI and BLE MIDI tested on hardware
- [ ] Final phone/tablet screenshots, 512×512 icon and 1024×500 feature graphic
- [ ] Play internal test completed before production rollout
