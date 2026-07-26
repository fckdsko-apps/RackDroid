# RackDroid — Privacy Policy / Informativa sulla privacy

_Last updated / ultimo aggiornamento: 2026-07-26_

## English

RackDroid is a modular synthesizer that runs entirely on your device.

- **No data collection.** RackDroid does not collect, store, or transmit any
  personal data, usage analytics, or telemetry.
- **Network access.** Patches, audio recordings, and settings are stored only
  in the app's private storage and, when you explicitly export them, in your
  device's `Documents/RackDroid/` folder. Nothing is ever uploaded.

  The version installed from Google Play makes **no network requests at all**
  and does not hold the INTERNET permission.

  The version downloaded from GitHub can check whether a newer release exists.
  It asks you first, and if you decline it never connects. If you accept, then
  at most once a day — and whenever you tap *Check for updates* — it requests
  the release information from `api.github.com` and, only if you confirm,
  downloads the new package from GitHub. In doing so GitHub can see your IP
  address and the fact that a RackDroid build asked; nothing about you, your
  patches or your usage is sent, and there is no account, identifier or
  analytics involved. You can decline at any time and the app stays offline.
- **Microphone.** The RECORD_AUDIO permission is used exclusively to route
  live audio input into the synthesizer's AUDIO module (e.g. to process an
  external instrument). Audio is processed in real time on the device and is
  never transmitted. You can deny this permission; the app then runs
  output-only.
- **Bluetooth.** Bluetooth permissions are used exclusively to connect
  Bluetooth LE MIDI keyboards/controllers you choose from the in-app scanner.
- **Notifications.** The notification permission is used only for the
  foreground-service notification that keeps audio running in the background.
- **No accounts, no ads, no third-party SDKs.**

Contact: patrik.meneguot@gmail.com

## Italiano

RackDroid è un sintetizzatore modulare che funziona interamente sul tuo
dispositivo.

- **Nessuna raccolta dati.** RackDroid non raccoglie, memorizza né trasmette
  dati personali, statistiche d'uso o telemetria.
- **Accesso alla rete.** Patch, registrazioni audio e impostazioni restano
  nello storage privato dell'app e, quando li esporti esplicitamente, nella
  cartella `Documents/RackDroid/`. Nulla viene mai caricato in rete.

  La versione installata da Google Play **non effettua alcuna richiesta di
  rete** e non dichiara il permesso INTERNET.

  La versione scaricata da GitHub può controllare se è uscita una versione più
  recente. Te lo chiede prima, e se rifiuti non si connette mai. Se accetti,
  al massimo una volta al giorno — e ogni volta che tocchi *Controlla
  aggiornamenti* — chiede le informazioni sulla release a `api.github.com` e,
  soltanto se confermi, scarica il nuovo pacchetto da GitHub. In quel momento
  GitHub può vedere il tuo indirizzo IP e il fatto che una copia di RackDroid
  abbia chiesto; non viene inviato nulla su di te, sulle tue patch o sul tuo
  utilizzo, e non esistono account, identificativi o statistiche. Puoi
  rifiutare in qualsiasi momento e l'app resta offline.
- **Microfono.** Il permesso RECORD_AUDIO serve esclusivamente a portare
  l'ingresso audio nel modulo AUDIO del sintetizzatore (es. per processare
  uno strumento esterno). L'audio è elaborato in tempo reale sul dispositivo
  e non viene mai trasmesso. Puoi negare il permesso: l'app funziona in sola
  uscita.
- **Bluetooth.** I permessi Bluetooth servono solo a collegare tastiere e
  controller MIDI Bluetooth LE che scegli dallo scanner in-app.
- **Notifiche.** Il permesso notifiche è usato solo per la notifica del
  servizio in primo piano che mantiene l'audio attivo in background.
- **Niente account, niente pubblicità, nessun SDK di terze parti.**

Contatto: patrik.meneguot@gmail.com
