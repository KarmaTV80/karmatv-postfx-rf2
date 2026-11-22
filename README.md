# KarmaTV PostFX per rFactor 2

Effetti grafici per rFactor 2 con un menu in gioco: profili pronti, regolazioni avanzate, automatismi guidati dalla telemetria (pioggia, replay, telecamera) e aggiornamenti automatici. È un add-on di [ReShade](https://reshade.me), che l'installer mette già pronto.

*English below.*

## Installazione

1. Scarica `KarmaTV-PostFX-Setup-x.y.z.exe` dall'[ultima versione](https://github.com/KarmaTV80/karmatv-postfx-rf2/releases/latest).
2. Chiudi rFactor 2 e avvia l'installer: trova da solo la cartella del gioco nelle librerie di Steam.
3. Avvia rF2, entra in pista e premi **Canc** per aprire il menu.

Windows può mostrare "Windows ha protetto il PC" perché l'installer non ha una firma digitale: scegli **Ulteriori informazioni**, poi **Esegui comunque**.

Per gli automatismi serve [rF2SharedMemoryMapPlugin](https://github.com/TheIronWolfModding/rF2SharedMemoryMapPlugin), attivo nelle opzioni Plugin di rF2. Senza il plugin gli effetti funzionano lo stesso.

Per disinstallare: **Impostazioni di Windows > App > App installate > KarmaTV PostFX per rFactor 2**. I tuoi profili restano, a meno che tu non scelga di eliminarli.

## Profili

| Profilo | Per cosa |
|---|---|
| Originale rF2 | Nessun effetto: la grafica del gioco |
| Karma FX | Guida su monitor |
| Replay Karma FX | Replay e regia: profondità di campo, motion blur. Si attiva da solo nei replay |
| VR Karma FX | Visore, con gli effetti adatti alla VR |

I profili fissi si possono modificare dal pulsante **Avanzate**, ma le modifiche si salvano solo come nuovo profilo. **Usa all'avvio** sceglie il profilo caricato all'apertura del gioco.

## Tasti

| Tasto | Azione |
|---|---|
| Canc | Apre e chiude il menu |
| + | Effetti on/off |
| , e . | Profilo precedente / successivo |
| - | Confronto A/B (a sinistra l'immagine originale) |

Si cambiano dal menu con **Personalizza i tasti**, anche con i pulsanti del volante.

## Aggiornamenti

All'avvio del gioco l'add-on controlla se su GitHub c'è una versione nuova. Se c'è, nel menu compare **Aggiorna**: l'installer viene scaricato, verificato con la sua impronta SHA-256 e installato quando chiudi rFactor 2. Il controllo invia a GitHub solo il nome e la versione del programma.

---

## English

Graphics effects for rFactor 2 with an in-game menu: ready-made profiles, advanced settings, telemetry-driven automation (rain, replays, camera) and automatic updates. It is a [ReShade](https://reshade.me) add-on; the installer sets ReShade up for you. The menu is in Italian.

1. Download `KarmaTV-PostFX-Setup-x.y.z.exe` from the [latest release](https://github.com/KarmaTV80/karmatv-postfx-rf2/releases/latest).
2. Close rFactor 2 and run the installer: it finds the game in your Steam libraries.
3. Start rF2, go on track and press **Delete** to open the menu.

Automation needs [rF2SharedMemoryMapPlugin](https://github.com/TheIronWolfModding/rF2SharedMemoryMapPlugin). Updates are checked on GitHub at game start and installed when you close the game.

---

## Sviluppo

Requisiti: Visual Studio 2022 (o Build Tools) con "Sviluppo di applicazioni desktop con C++", CMake 3.24+, Git. Per l'installer: [Inno Setup 6](https://jrsoftware.org/isinfo.php).

```powershell
cmake --preset x64
cmake --build --preset release
```

Se git rifiuta il clone di ReShade per "dubious ownership" (dischi senza proprietario dei file):

```powershell
$env:GIT_CONFIG_COUNT="1"; $env:GIT_CONFIG_KEY_0="safe.directory"; $env:GIT_CONFIG_VALUE_0="*"
```

| Script | Cosa fa |
|---|---|
| `tools\install.ps1 -Rf2Bin64 "<rF2>\Bin64"` | Installa la build locale in rF2 (ReShade, add-on, shader, LUT, carattere) |
| `tools\uninstall.ps1 -Rf2Bin64 "<rF2>\Bin64"` | Toglie quello che ha installato `install.ps1` |
| `tools\package.ps1` | Compila e crea `dist\KarmaTV-PostFX-Setup-<versione>.exe` con la sua impronta `.sha256.txt` |

Per pubblicare una versione: aggiorna `VERSION` in `CMakeLists.txt`, lancia `tools\package.ps1` e crea su GitHub una release con tag `v<versione>` e i due file di `dist` (`.exe` e `.exe.sha256.txt`). L'add-on cerca proprio questi due file.

```text
src/
  addon_main.cpp   ingresso add-on ed eventi ReShade
  app.*            stato globale, profili, aggiornamento per frame
  core/            catalogo effetti, preset .ini, log
  telemetry/       shared memory di rF2 (punteggi, telecamera)
  rules/           automatismi da telemetria
  input/           tasti rapidi e pulsanti del volante
  perf/            profiler GPU, picchi
  render/          applicazione degli effetti, profondità, aggancio prima dell'HUD
  ui/              menu in gioco, tema, logo
  update/          controllo e installazione degli aggiornamenti
shaders/           KarmaTV_<ordine>_<Effetto>.fx
tools/             installazione, pacchetto, installer, creazione LUT
```

## Licenza

Gratuito, tutti i diritti riservati: vedi [LICENSE.txt](LICENSE.txt). ReShade è distribuito con licenza BSD-3-Clause, il carattere Barlow Condensed con SIL Open Font License.
