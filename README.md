English version at: [`ENGLISH_README`](https://github.com/nowheel/RackDroid/blob/main/README.en.md)
# RackDroid — sintetizzatore modulare per Android

Port **non ufficiale** del motore di [VCV Rack 2](https://vcvrack.com) su Android,
costruito sui sorgenti upstream non modificati (`third_party/Rack`, pinnato a
v2.6.4) più un layer di porting nativo e un'interfaccia pensata per il tocco.
Non affiliato a VCV.

Costruisci patch modulari sul telefono: colleghi oscillatori, filtri, inviluppi,
sequencer e altro con cavi virtuali, esattamente come in un rack hardware.

> **Versione 0.1.0** · repo: [`nowheel/RackDroid`](https://github.com/nowheel/RackDroid)

## Cosa fa

- **Motore audio nativo** a bassa latenza (Oboe/AAudio), full-duplex.
- **66 moduli di base** sempre presenti: **Core** (Audio/MIDI), **Fundamental**
  (39: VCO, VCF, VCA, ADSR, LFO, SEQ-3, Delay, Mixer, Scope, Quantizer…) e
  **RackDroid Drums** (14 voci di batteria originali nella tradizione dell'808).
- **Interfaccia touch**: trascini per creare cavi e spostare moduli, pizzichi
  per lo zoom, tieni premuta una manopola per digitare un valore.
- **Barra strumenti a vetro** con menu (File/Modifica/Visualizza/Motore/Aiuto) e
  tool: palette moduli, gestore plugin, annulla/ripeti, blocco layout/parametri,
  MIDI, tastiera, registrazione, info. Si richiude in una linguetta.
- **Palette dei moduli**: chip per categoria (VCO, LFO, VCF, VCA, ENV, SEQ,
  DRUM, MIX, FX, NOISE, QNT, MIDI, UTIL), anteprime trascinabili sul rack, tocco
  per inserire al centro, pallino **ⓘ** con nome/descrizione/tag di ogni modulo.
  Si chiude con lo swipe verso il basso.
- **Menu a bottom-sheet** con swipe-giù per chiudere; cursori di **tensione** e
  **opacità dei cavi** nel menu Visualizza.
- **Tastiera musicale** a schermo (con cambio ottava e chiusura ✕) e **MIDI
  Bluetooth LE**.
- **Registrazione** dell'uscita su file **WAV** in `Documents/RackDroid/`.
- **30 tutorial** guidati passo-passo su 5 livelli, più una guida per argomenti.

## Moduli aggiuntivi (.rdmod)

Oltre ai moduli di base, puoi aggiungere pacchetti (Bogaudio, Valley, Audible,
Impromptu, Befaco, HetrickCV…) **al volo**, senza aggiornare l'app:

- **Dall'app**: tool *Gestore moduli* → *Installa da file* → scegli uno o più
  file `.rdmod`. Vengono caricati subito; li disinstalli dallo stesso gestore.
- **Da cartella**: copia i `.rdmod` in `Android/data/org.rackdroid/files/Modules/`
  e riavvia.

Formato del pacchetto, meccanismo di caricamento nativo e istruzioni per
**creare** un plugin: vedi **[MODULES.md](MODULES.md)** e il manuale in
**[docs/rackdroid-manuale.pdf](docs/rackdroid-manuale.pdf)**.

## Build

Progetto Gradle alla radice del repo (arm64-v8a, `minSdk 33`). I sorgenti
`third_party/` (Rack v2.6.4, Oboe, tutti i plugin) sono **vendorizzati nel
repo**: un clone pulito compila così com'è, senza init di submodule.

```sh
export JAVA_HOME=~/jdk21; export ANDROID_HOME=~/android-sdk
gradle assembleRelease -PdevKeystore    # gradle 8.7+, oppure apri in Android Studio
```

- `-PdevKeystore` firma con la chiave di sviluppo pubblica (continuità di
  aggiornamento per il sideload; per Play usare una chiave privata).
- L'APK di base pesa ~40 MB: contiene solo i moduli base; CMake compila comunque
  tutti i plugin, ma i `.so` non-base sono esclusi dall'APK e distribuiti come
  `.rdmod` (`packaging.jniLibs.excludes`, vedi `scripts/make_rdmods.sh`).

## Struttura

```
app/            modulo Android (Gradle, manifest, MainActivity + UI Kotlin)
native/
  CMakeLists.txt  build motore Rack + dipendenze + port layer
  port/           codice del porting (audio Oboe, menu, browser, plugin loader…)
  host/           smoke test del motore/UI su Linux
drums/          RackDroid Drums (pacchetto first-party, codice + pannelli originali)
graphics/       grafica originale (pannelli, thumbnail, ComponentLibrary rifatta)
third_party/    Rack, Oboe e sorgenti dei plugin (upstream intatti)
scripts/        setup.sh (sorgenti) · make_rdmods.sh (impacchetta i .rdmod)
docs/           manuale utente (PDF + sorgente HTML)
MODULES.md      formato .rdmod, caricamento e creazione dei plugin
```

Principio guida: **zero patch ai sorgenti di Rack**. Tutto il codice
piattaforma-specifico vive in `native/port/`; i file desktop-only sono esclusi
dal build e rimpiazzati, così l'aggiornamento a nuove versioni upstream resta un
bump del submodule.

## Licenze, trademark e pubblicazione — importante

- Il codice di Rack è **GPLv3**: questo port è GPLv3 e i sorgenti completi sono
  nel repository (obbligo di licenza soddisfatto ✓).
- **Trademark**: l'app si presenta come "RackDroid" (icona propria, stringhe
  rebrandizzate); il nome/logo "VCV" non è usato ✓.
- **Grafica**: la ComponentLibrary e i pannelli Core originali sono
  **CC BY-NC-ND 4.0** (non commerciale). RackDroid usa grafica **rifatta**
  (`graphics/`, GPLv3) al loro posto per essere distribuibile; i plugin
  Fundamental/Bogaudio ecc. sono GPLv3 con grafica inclusa ✓.
- **Firma**: `keystore/rackdroid.keystore` è una chiave di **sviluppo** con
  password pubblica (`rackdroid`) — continuità di aggiornamento per il sideload,
  NON autenticità. Per uno store generare una chiave privata (o Play App
  Signing).
- **Google Play**: distribuire codice nativo eseguito da **fuori** Play viola le
  policy; la cartella `.rdmod` / l'installazione da file sono per la build
  sideload/GitHub. Per Play, consegnare i pacchetti extra via *asset packs*.

---

RackDroid è un port di VCV Rack (GPLv3). Non affiliato né approvato da VCV.
