# RackDroid — port Android (non ufficiale) di VCV Rack

Port **da zero** del motore di VCV Rack 2 su Android, costruito sui sorgenti
upstream non modificati (`third_party/Rack`, submodule pinnato a v2.6.4) più un
layer di porting nativo. Non è affiliato a VCV.

## Stato attuale — fase 1 (motore headless) ✅

Cosa funziona oggi:

- **Motore Rack compilato fuori dal Makefile ufficiale** con CMake, in un
  "engine subset" senza UI: engine DSP, framework plugin, settings, sistema,
  audio/MIDI framework. Verificato dallo smoke test host (`rack_smoke`):
  1 secondo di audio (48 kHz, blocchi da 256) processato e shutdown pulito.
- **Driver audio Oboe** (`native/port/audio_oboe.cpp`): implementa
  `rack::audio::Driver/Device` su AAudio full-duplex low-latency, con
  degradazione a solo output senza permesso microfono e riapertura automatica
  su cambio route (cuffie, Bluetooth).
- **App Android** (NativeActivity + NDK): all'avvio estrae le risorse di
  sistema dall'APK, avvia il motore headless, apre una superficie EGL/GLES3 e
  suona un tono di test a 440 Hz attraverso il driver Oboe — la prova
  end-to-end del percorso audio.

Cosa NON funziona ancora: UI (nanovg/GLES3), plugin Core reale, caricamento
patch, MIDI hardware, plugin di terze parti. Roadmap completa in
[PORTING.md](PORTING.md).

## Build

```sh
./rack-android/scripts/setup.sh      # submodule (Rack v2.6.4, Oboe 1.10, dep di Rack)
```

**Smoke test host** (Linux, nessun SDK Android richiesto):

```sh
cmake -S rack-android/native -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-host
./build-host/rack_smoke
```

**App Android** (SDK + NDK r26+, oppure Android Studio):

```sh
cd rack-android
gradle wrapper --gradle-version 8.9   # solo la prima volta
./gradlew assembleDebug
```

Requisiti attuali: `minSdk 33` (Android 13, per `<execinfo.h>` in bionic),
solo `arm64-v8a`. Le dipendenze C (jansson, zstd, libarchive) sono scaricate e
compilate da CMake via FetchContent con tag pinnati.

## Struttura

```
rack-android/
├── app/                    modulo Android (Gradle, manifest, MainActivity)
├── native/
│   ├── CMakeLists.txt      build del motore Rack + dipendenze + port layer
│   ├── compat/GL/glew.h    shim GLEW→GLES3 per gli header di Rack
│   ├── port/               codice del porting:
│   │   ├── main_android.cpp       entry NativeActivity, EGL, bring-up Rack
│   │   ├── audio_oboe.cpp         driver rack::audio su Oboe
│   │   ├── asset_extract.cpp      estrazione risorse dall'APK (libarchive)
│   │   ├── context_headless.cpp   Context senza Scene/Window/Patch
│   │   ├── model_headless.cpp     plugin::Model senza menu contestuali
│   │   └── *_stub.*               network, osdialog, Core, clipboard GLFW
│   └── host/main_host.cpp  smoke test del motore su Linux
├── third_party/Rack        submodule: VCV Rack v2.6.4 (sorgenti intatti)
├── third_party/oboe        submodule: Google Oboe 1.10.0
└── PORTING.md              roadmap tecnica a fasi
```

Principio guida: **zero patch ai sorgenti di Rack**. Tutto ciò che è
piattaforma-specifico vive in `native/port/`; i file desktop-only di Rack sono
esclusi dal build e rimpiazzati da equivalenti, così l'aggiornamento a nuove
versioni upstream resta un bump del submodule.

## Licenze, trademark e pubblicazione — importante

Stato attuale ai fini della distribuzione:

- Il codice di Rack è **GPLv3**: questo port è GPLv3 e i sorgenti completi
  sono in questo repository (obbligo di licenza soddisfatto ✓).
- **Trademark**: l'app si presenta come "RackDroid" (icona propria, stringhe
  a runtime rebrandizzate); il nome/logo "VCV" non è usato ✓. Il termine
  generico "Rack" segue il precedente di miRack.
- **Component Library** (res/ComponentLibrary, la grafica di manopole/jack):
  licenza **CC BY-NC-ND 4.0** — uso non commerciale con attribuzione.
  Distribuzione **gratuita** con attribuzione: difendibile; qualsiasi
  monetizzazione (prezzo, pubblicità, IAP): **no** senza sostituire la
  grafica. Decisione e verifica legale spettano a chi pubblica. Stesso
  discorso per i pannelli Core. I plugin Fundamental/Bogaudio sono GPLv3
  (grafica inclusa) ✓.
- **Firma**: `keystore/rackdroid.keystore` è un keystore di sviluppo con
  password pubblica (`rackdroid`): garantisce continuità di aggiornamento
  per il sideload, NON autenticità. Per uno store generare un keystore
  privato (o usare Play App Signing) e sostituirlo in app/build.gradle.kts.
- Build release firmata: `./gradlew assembleRelease`.
