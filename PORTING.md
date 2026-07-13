# Roadmap tecnica del porting

Stato: **fase 1 completata**. Le fasi sono ordinate in modo che ognuna produca
qualcosa di verificabile su dispositivo.

## Fase 1 — Motore headless ✅

Obiettivo: il motore DSP di Rack compila e gira su toolchain non-desktop.

- [x] Rack v2.6.4 + Oboe vendorizzati come submodule, sorgenti upstream intatti
- [x] Build CMake dell'"engine subset" (~30 file di src/ + dep sorgente:
      nanovg core, pffft, tinyexpr, speexdsp resampler, osdialog common)
- [x] Dipendenze C via FetchContent pinnate: jansson 2.14, zstd 1.5.6,
      libarchive 3.7.4 (tar+zip, per patch .tar.zst e asset APK)
- [x] Sostituzioni headless: `context_headless` (Context senza UI),
      `model_headless` (Model senza menu), stub network/osdialog/Core/GLFW,
      shim `GL/glew.h`
- [x] Smoke test host: bring-up completo, 1 s di audio in ~2 ms, shutdown pulito
- [x] Driver audio Oboe (`rack::audio::Driver`), full-duplex, error recovery
- [x] App NativeActivity: estrazione asset, engine avviato, EGL/GLES3, tono di test

Verifica su dispositivo: installare `assembleDebug`; schermo grigio scuro,
tono a 440 Hz, log `rackdroid` in logcat.

## Fase 2 — Rendering della scena ✅ (build; da validare su dispositivo)

Obiettivo: vedere il rack renderizzato e interagirci.

- [x] `dep_android.cpp`: `NANOVG_GLES3_IMPLEMENTATION` + nanosvg + stb;
      blendish compilato dal `.c` del fork di Rack (con fix inline C99)
- [x] `window_android.cpp`: `window::Window` completo su EGL/ANativeWindow —
      contesto GLES3 con stencil, surface sostituibile a caldo (rotazione,
      background) senza perdere il contesto GL né la Scene, pixelRatio dalla
      densità display, font/image cache come upstream
- [x] Stack UI upstream compilato intero: `src/app`, `src/ui`, `src/widget`
      (tranne `OpenGlWidget.cpp`, sostituito: usava GL fixed-function),
      `src/window/Svg.cpp`, `src/patch.cpp`, `src/history.cpp`,
      `src/context.cpp`, `src/library.cpp`, `src/keyboard.cpp`;
      rimossi i sostituti headless dal build Android
- [x] Plugin Core reale (`src/core/`): il modulo Audio pilota il driver Oboe
      (unico driver registrato → default automatico); TestTonePort rimosso
- [x] `APP->patch->launch("")` all'avvio: autosave o template.vcv, con
      autosave alla chiusura dell'app
- [ ] Clipboard reale via JNI al posto degli stub GLFW (copia/incolla preset)
- [ ] Validazione su dispositivo (rendering, FramebufferWidget/FBO su ES3)

## Fase 3 — Input touch (base fatta in fase 2)

- [x] Un dito = mouse sinistro (hover+press/drag/release): manopole, cavi, menu
- [x] Long-press fermo (0,6 s) = click destro → menu contestuali
- [x] Due dita = pan (scroll); pinch = zoom (Ctrl+scroll emulato)
- [ ] Tastiera software per i TextField (JNI, `showSoftInput`)
- [ ] Tolleranze di hit-test aumentate per il tocco (port grabbing)
- [ ] Rifiniture: inerzia dello scroll, doppio tap, drag di moduli dal browser

## Fase 4 — MIDI, dialoghi e file ✅ (build; da validare su dispositivo)

- [x] Driver `rack::midi` su **AMidi**: MainActivity apre i device MidiManager
      (USB/BLE/virtuali, hotplug incluso) e li passa al driver nativo; input
      con parser running-status su thread di polling, output supportato
- [x] **Dialoghi Android** al posto degli stub osdialog: messaggi OK/Annulla,
      prompt di testo, salvataggio patch (nome file) e apertura (lista dei
      .vcv nella cartella utente) — File > Salva/Apri ora funzionano
- [x] **Clipboard reale** (ClipboardManager via JNI) per copia/incolla
      preset, moduli e testo
- [x] **Editing testo touch**: tap su un TextField apre un prompt Android con
      tastiera di sistema (ricerca nel browser moduli, Notes, ...)
- [ ] Storage Access Framework per import/export fuori dalla sandbox app
- [ ] Import di patch .vcv scaricate (intent filter sul tipo file)

## Fase 5 — Ecosistema plugin (primo passo fatto)

- [x] Meccanismo di **plugin bundled** (`port/static_plugins.cpp`): ogni
      plugin è una vera shared library nell'APK (lib dir: lì dlopen È
      permesso), caricata con RTLD_LOCAL come su desktop. Necessario: plugin
      diversi riusano nomi globali (pluginInstance, modelVCO, vtable di
      VCOWidget...) che in un link statico si aliasano tra loro → crash.
      Architettura finale: librack_engine.so + librackdroid.so (app) +
      libplugin_*.so, manifest/res estratti in systemDir/plugins/<slug>/
- [x] **Fundamental 2.6.4** compilato nell'APK (40 moduli: VCO, VCF, VCA,
      LFO, ADSR, Delay, mixer, SEQ3, Scope, ...) + libsamplerate vendorizzata;
      la patch template di default ora si carica intera
- [x] Fix libarchive/zstd: i risultati dei check sono pre-seedati, altrimenti
      libarchive ripiega sul programma esterno `zstd` (inesistente su
      Android) e i file .vcv non si aprono
- [x] **Bogaudio 2.6.46** nell'APK (111 moduli: oscillatori, filtri,
      envelope, mixer, analyzer, sequencer...). Totale: 162 moduli con Core.
      Verificato dal test host `rack_ui_smoke N --all-modules`, che istanzia
      e renderizza ogni modulo registrato come farebbe il browser
- [ ] Altri plugin GPL-compatibili con lo stesso meccanismo (Valley, ...) —
      attenzione alle licenze non commerciali di alcuni
- [ ] Toolchain generica NDK per plugin fuori dall'APK (solo distribuzione
      fuori Play Store)
- [ ] Niente store/account VCV su Android (network_stub resta)

## Debiti tecnici correnti

- `minSdk 33` per `<execinfo.h>` (system::getStackTrace): scendere richiede
  uno shim backtrace (es. unwind.h) 
- La rejection del manifest Core in fase 1 è attesa (stub senza modelli)
- Il branding va cambiato prima di qualunque distribuzione (trademark VCV,
  asset CC BY-NC: vedi README)
- `gradle wrapper` non è committato: generarlo alla prima build
