# Publishing RackDroid

Everything needed to put RackDroid on the Google Play Store (or sell it via
sideload). The app is legally distributable **commercially**: engine/modules
are GPL-3.0 (source published), all VCV CC-BY-NC graphics are replaced with
original artwork, and the only third-party art (Bogaudio, CC BY-SA) is
attributed in-app (ⓘ button) and in NOTICE-graphics.md.

## Build artifacts

```sh
cd rack-android
export ANDROID_HOME=/path/to/android-sdk
./gradlew assembleRelease   # signed APK  -> app/build/outputs/apk/release/
./gradlew bundleRelease     # AAB (Play)  -> app/build/outputs/bundle/release/
```

Play Store accepts only the **AAB**. Sideloading/other stores use the APK.

## Signing key — REQUIRED before publishing

`keystore/rackdroid.keystore` is a development key with a public password; it
only gives update continuity for sideloads. **Before publishing to Play**:

1. Generate a private upload key and keep it secret (or enrol in Play App
   Signing and let Google manage the app-signing key):
   ```sh
   keytool -genkeypair -v -keystore my-upload.keystore -alias upload \
     -keyalg RSA -keysize 2048 -validity 10000
   ```
2. Point `signingConfigs.release` in `app/build.gradle.kts` at it, ideally via
   `local.properties` / env vars so the secret isn't committed.

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
> share any personal data. It has no analytics, no ads, no accounts, and no
> network communication. Audio input (microphone) and MIDI/Bluetooth
> permissions are used only locally, in real time, to process audio and
> connect controllers; nothing is recorded or sent anywhere. Patches you save
> stay in the app's private storage until you export them yourself.
> Contact: <your email>

## Store listing (draft)

- **Title**: RackDroid — Modular Synth
- **Short description**: A powerful modular synthesizer for Android. 150+
  modules, low-latency audio, USB & Bluetooth MIDI.
- **Full description**:
  > RackDroid brings full modular synthesis to your phone and tablet. Patch
  > oscillators, filters, envelopes, sequencers and effects together with
  > virtual cables and build your own sounds from scratch.
  >
  > • 150+ modules (Fundamental + Bogaudio): VCOs, VCFs, VCAs, LFOs, ADSRs,
  >   mixers, sequencers, scopes, logic, and much more
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

## Pre-launch checklist

- [ ] Private signing key (not the dev keystore)
- [ ] Privacy policy hosted + URL in listing
- [ ] Source repo public and matching the released build (GPLv3)
- [ ] `minSdk` acceptable (currently 33 = Android 13+; lower to widen reach)
- [ ] Test on several devices (GPU rendering, audio latency, MIDI)
- [ ] Screenshots + feature graphic
