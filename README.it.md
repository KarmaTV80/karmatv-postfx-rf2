# KarmaTV PostFX for rFactor2

[English](README.md) · **Italiano** · [Español](README.es.md) · [Français](README.fr.md) · [Deutsch](README.de.md) · [Português](README.pt.md)

Effetti grafici per rFactor 2 con un menu in gioco: profili pronti, regolazioni avanzate, automatismi guidati dalla telemetria (pioggia, replay, telecamera) e aggiornamenti automatici. È un add-on di [ReShade](https://reshade.me), che l'installer mette già pronto. Il menu è in inglese, italiano, spagnolo, francese, tedesco e portoghese.

## Installazione

1. Scarica `KarmaTV-PostFX-Setup-x.y.z.exe` dall'[ultima versione](https://github.com/KarmaTV80/karmatv-postfx-rf2/releases/latest).
2. Chiudi rFactor 2 e avvia l'installer: trova da solo la cartella del gioco nelle librerie di Steam.
3. Avvia rF2, entra in pista e premi **Canc** per aprire il menu.

Windows può mostrare "Windows ha protetto il PC" perché l'installer non ha una firma digitale: scegli **Ulteriori informazioni**, poi **Esegui comunque**.

Per gli automatismi serve [rF2SharedMemoryMapPlugin](https://github.com/TheIronWolfModding/rF2SharedMemoryMapPlugin), attivo nelle opzioni Plugin di rF2. Senza il plugin gli effetti funzionano lo stesso.

Per disinstallare: **Impostazioni di Windows > App > App installate > KarmaTV PostFX for rFactor2**. I tuoi profili restano, a meno che tu non scelga di eliminarli.

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

Si cambiano dal menu con **Personalizza i tasti**, anche con i pulsanti del volante. La lingua si cambia con il pulsante nell'intestazione del menu.

## Aggiornamenti

All'avvio del gioco l'add-on controlla se su GitHub c'è una versione nuova. Se c'è, nel menu compare **Aggiorna**: l'installer viene scaricato, verificato con la sua impronta SHA-256 e installato quando chiudi rFactor 2. Il controllo invia a GitHub solo il nome e la versione del programma.

Per compilare e contribuire alle traduzioni vedi la sezione *Development* del [README inglese](README.md#development).

## Licenza

Gratuito, tutti i diritti riservati: vedi [LICENSE.txt](LICENSE.txt). ReShade è distribuito con licenza BSD-3-Clause, il carattere Barlow Condensed con SIL Open Font License.
