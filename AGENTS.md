# RackDroid — istruzioni per gli agenti

File di contesto per gli IDE agentici (Antigravity/Gemini, Cursor, Copilot
Workspace, Codex, Continue…). Claude Code legge `CLAUDE.md`, che contiene le
stesse regole: se cambi una regola qui, cambiala anche là.

Da leggere prima di lavorare: `README.md`, `PORTING.md`, `MODULES.md`,
`PUBLISHING.md`. Per lavori di rilascio anche `PRIVACY.md`.

## Cos'è questo progetto

Porting Android **non ufficiale** del motore di VCV Rack 2: un sintetizzatore
modulare che gira nativo su telefono, con il tocco al posto del mouse. Kotlin per
l'interfaccia Android, C++ per il motore e per lo strato di porting.

- Release candidate attuale: **0.1.2** (`versionCode 3`).
- Android 10+ (`minSdk 29`), target/compile SDK 35, **solo arm64-v8a**.
- NDK 27 con supporto pagine da 16 KB: è un requisito di Google Play.
- APK base: Core + Fundamental + RackDroid Drums (66 moduli). Altri 21 pacchetti
  di moduli si distribuiscono a richiesta come file `.rdmod`.

## Le tre regole che non si violano

1. **Non modificare `third_party/Rack`.** Il codice upstream resta identico
   all'originale: la GPLv3 e la sostenibilità del porting dipendono da questo.
   Le sostituzioni Android vivono in `native/port/` e si scelgono in
   `native/CMakeLists.txt`. Se serve cambiare un sorgente upstream, si genera
   una copia modificata a build time con `string(REPLACE …)` in CMake — nel
   file c'è già più di un esempio da cui copiare l'idea.
2. **Motore, scena e widget sono solo del render thread.** Tutto ciò che Java
   deve leggere va ripubblicato in atomiche dal render thread; tutto ciò che
   Java vuole fare va accodato e applicato lì. Il contesto di Rack (`APP`) è
   thread-local: leggerlo dal thread UI non dà un errore, dà un SIGSEGV.
   Vedi i ponti già esistenti in `browser_native.cpp` e `menu_native.cpp`.
3. **Il repository può contenere lavoro locale volontario.** Guarda sempre
   `git status` e `git diff` prima di modificare, e non scartare mai modifiche
   che non hai fatto tu senza che l'utente lo chieda esplicitamente.

## Build

Serve un SDK Android vero nell'ambiente; non committare `local.properties` con
percorsi della tua macchina.

```sh
export ANDROID_HOME=/percorso/android-sdk
export ANDROID_SDK_ROOT="$ANDROID_HOME"
./gradlew assembleSideloadRelease -PdevKeystore --no-daemon
```

Esce `app/build/outputs/apk/sideload/release/app-sideload-release.apk`. Il
keystore di sviluppo è pubblico e serve solo alla continuità degli aggiornamenti
sideload.

Ci sono **due distribuzioni**: `sideload` (GitHub) può aggiornarsi da sola, e
quindi dichiara `INTERNET` e `REQUEST_INSTALL_PACKAGES`; `play` non ha nessuna
delle due e ha l'updater ridotto a uno stub, perché Play vieta a un'app di
aggiornarsi da sé. Compila `assembleSideloadRelease` / `bundlePlayRelease`: un
`assembleRelease` nudo costruisce entrambe.

Una build completa (tutti i plugin arm64 + i 21 pacchetti) è:

```sh
./gradlew assembleSideloadRelease bundlePlayRelease -PdevKeystore -PallPlugins --no-daemon
scripts/make_rdmods.sh /tmp/rdmods
```

Nota sui tempi: una build da zero non è istantanea (minuti, non secondi) e
`grep -c` su un log senza errori esce con stato 1 — non è la build che è
fallita. Guarda `BUILD SUCCESSFUL` e il codice di uscita di Gradle.

## Verifica

```sh
scripts/host_smoke.sh     # costruisce e disegna ogni modello registrato
scripts/device_smoke.sh   # smoke test su device autorizzato
```

Non sostituire `host_smoke.sh` con `rack_ui_smoke --all-modules`: lo script
prepara i manifest e imposta `RACKDROID_EXTRA_PLUGINS`, senza i quali si
registrano solo i modelli Core e il test passa verde senza aver provato niente.
Il roster attuale è di 1004 modelli; se lo cambi volontariamente aggiorna il
numero in `PUBLISHING.md`.

## Provare sul telefono

L'emulatore qui non è una via: senza KVM l'immagine Google Play è così lenta che
`system_server` va in ANR e muore, e le immagini ATD non hanno traduzione
`arm64-v8a`, quindi l'APK non si installa nemmeno. Si prova su hardware, via adb.

```sh
adb install -r app/build/outputs/apk/sideload/release/app-sideload-release.apk
adb shell svc power stayon usb
```

- `input swipe` **non** produce una pressione lunga: parte subito in movimento,
  quindi `onLongClick` non scatta. Per un long-press servono i singoli eventi:
  `adb shell input motionevent DOWN x y`, attesa, `MOVE`, `UP`.
- `input` lavora in **pixel di schermo**; il codice nativo in unità di scena
  (`scena = pixel / window->pixelRatio`).
- **Le coordinate scadono.** Uno zoom-to-fit, una panoramica, un ricaricamento
  della patch invalidano ogni punto memorizzato, e i tocchi finiscono su
  qualunque cosa si sia spostata lì: su questo progetto è già capitato di
  trascinare moduli a caso in mezzo a un test. Rifai lo screenshot e ricalcola
  dopo ogni cambio di vista.
- Un mismatch di firma significa che sul telefono c'è una build firmata con
  un'altra chiave: l'unica via è disinstallare, che cancella patch e pacchetti
  `.rdmod`. **Chiedi prima.**
- Log: `adb logcat -s rackdroid:V` e, dentro l'app, `user/log.txt` — quest'ultimo
  è quello che un utente può mandarti, quindi le diagnostiche importanti devono
  finire in entrambi.

**Cosa vuol dire "verificato" qui**: compilare non è eseguire, e "l'API è
corretta" non è "l'ho visto funzionare". Se un percorso non l'hai potuto
esercitare (multi-touch, qualità audio, qualunque cosa richieda orecchie), dillo
chiaramente invece di lasciare che il riassunto faccia sembrare il contrario.
Su questo progetto sono già stati consegnati come verificati dei fix di cui era
stata controllata solo la build — uno andava in crash al primo avvio.

## Convenzioni di interfaccia

- I bersagli di tocco restano da dito: niente aree da mouse.
- La card in alto ha una riga di menu e una griglia strumenti di 2 righe × 8
  colonne. Riga 1: moduli, pacchetti, parcheggio cavi, tema, MIDI, tastiera,
  registrazione, info. Riga 2: annulla, ripeti, selezione multipla, copia,
  incolla, cestino, i due lucchetti.
- La selezione multipla ridefinisce un dito: un tocco su un modulo lo aggiunge o
  lo toglie dalla scelta, una pressione lunga lo prende per spostarlo, e il menu
  del modulo resta chiuso.
- Cancellare dalla toolbar chiede sempre conferma e dice quanti moduli sparisce.
- Vanno preservati: panoramica a un dito, panoramica/zoom a due dita, inerzia
  dello scorrimento, menu contestuali da pressione lunga, lucchetti, selezione
  multipla, barra di parcheggio dei cavi.
- **Stringhe inglesi e italiane si aggiornano insieme.** Entrambe le lingue sono
  di prima classe (`values/` e `values-it/`).

## Il tour dell'interfaccia (`HelpUi.kt` + `native/port/tour_demo.cpp`)

Venti passi che spiegano lo schermo e lo **fanno vedere**: inquadrano i moduli,
aprono ogni menu a turno e la palette, spostano un modulo, ingrandiscono e
scorrono il rack, tracciano un cavo con i jack compatibili accesi, accendono la
selezione multipla.

- Il tour è una **dimostrazione, non una modifica**: può partire sopra la patch
  su cui l'utente sta lavorando, quindi non tocca la cronologia, non lascia
  niente selezionato, non consegna nessun cavo al motore (quello disegnato è un
  cavo incompleto) e rimette posizioni, zoom e offset esatti. Il ripristino gira
  anche prima di qualunque salvataggio automatico.
- Gli spotlight si misurano dalle viste vere e si convertono nello spazio dello
  scrim. **Mai** dimensionarli da `displayMetrics` o da dp fissi: devono valere
  in verticale, in orizzontale e in una finestra desktop.
- Anima sul tempo (`system::getTime()`), non sui frame: una versione a frame
  finiva in una frazione di secondo su hardware veloce.
- Dopo aver guidato il cursore, passa su un punto fuori da ogni widget:
  `handleLeave()` di proposito non toglie l'hover, quindi il tooltip dell'ultimo
  jack resterebbe a video per il resto della sessione.

## Aggiornamenti (solo build sideload)

Percorso: release GitHub → download → verifica (nome pacchetto, `versionCode`
maggiore, **firma identica**) → sessione `PackageInstaller`. Il `PendingIntent`
di stato arriva a `MainActivity`, e `AppUpdates.onNewIntent` **deve** lanciare
l'`EXTRA_INTENT` che accompagna `STATUS_PENDING_USER_ACTION`: senza quello
Android non mostra la sua schermata di conferma e l'aggiornamento non si installa
mai, senza un solo messaggio a schermo che spieghi perché. Provato da capo a
fondo su hardware (0.1.0 → 0.1.1 dalla release vera).

Le note di rilascio sono Markdown e il dialogo è testo semplice: passano da
`plainNotes()`.

## Sicurezza del rilascio

- Non committare mai: keystore privati, password, credenziali, `.claude/`,
  percorsi dell'SDK, output di build, catture di logcat, screenshot di device
  con informazioni personali.
- Il sorgente pubblico deve stare all'esatto commit rilasciato (GPLv3).
- `PRIVACY.md`, i testi dello store, il conteggio dei moduli, gli screenshot e i
  numeri di versione vanno tenuti sincronizzati con l'artefatto pubblicato.
- Non dichiarare validata una funzione di device se non è stata provata su
  hardware: metti device, livello di API e risultato in `PUBLISHING.md` o nelle
  note di rilascio.

## Come si scrive qui

- Commenti e messaggi di commit spiegano **perché**, non cosa: il cosa si legge
  nel codice. I commenti che valgono di più in questo repository sono quelli che
  registrano una trappola già pagata una volta (il tooltip che resta, i frame
  che scorrono troppo veloci, le coordinate che scadono).
- Il codice nuovo somiglia a quello intorno: stessa densità di commenti, stessi
  nomi, stessi idiomi. Tabulazioni, non spazi, nei file Kotlin e C++ di questo
  progetto.
- Niente riscritture opportunistiche fuori dallo scopo del compito.
