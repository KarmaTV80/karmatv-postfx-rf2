# KarmaTV PostFX for rFactor2

[English](README.md) · [Italiano](README.it.md) · [Español](README.es.md) · [Français](README.fr.md) · **Deutsch** · [Português](README.pt.md)

Grafikeffekte für rFactor 2 mit einem Menü im Spiel: fertige Profile, erweiterte Einstellungen, telemetriegesteuerte Automatik (Regen, Replays, Kamera) und automatische Updates. Es ist ein [ReShade](https://reshade.me)-Add-on; das Installationsprogramm richtet ReShade für dich ein. Das Menü gibt es auf Englisch, Italienisch, Spanisch, Französisch, Deutsch und Portugiesisch.

## Installation

1. Lade `KarmaTV-PostFX-Setup-x.y.z.exe` von der [neuesten Version](https://github.com/KarmaTV80/karmatv-postfx-rf2/releases/latest) herunter.
2. Beende rFactor 2 und starte das Installationsprogramm: Es findet das Spiel in deinen Steam-Bibliotheken.
3. Starte rF2, fahre auf die Strecke und drücke **Entf**, um das Menü zu öffnen.

Windows zeigt eventuell „Der Computer wurde durch Windows geschützt“, weil das Installationsprogramm nicht digital signiert ist: Wähle **Weitere Informationen** und dann **Trotzdem ausführen**.

Die Automatik braucht [rF2SharedMemoryMapPlugin](https://github.com/TheIronWolfModding/rF2SharedMemoryMapPlugin), aktiviert in den Plugin-Optionen von rF2. Die Effekte funktionieren auch ohne.

Deinstallieren: **Windows-Einstellungen > Apps > Installierte Apps > KarmaTV PostFX for rFactor2**. Deine Profile bleiben erhalten, außer du entscheidest dich, sie zu löschen.

## Profile

| Profil | Wofür |
|---|---|
| rF2 Original | Keine Effekte: die Grafik des Spiels |
| Karma FX | Fahren am Monitor |
| Replay Karma FX | Replays und Regie: Tiefenschärfe, Bewegungsunschärfe. Schaltet sich in Replays von selbst ein |
| VR Karma FX | Headset, mit VR-tauglichen Effekten |

Feste Profile lassen sich über **Erweitert** ändern, gespeichert werden die Änderungen aber nur als neues Profil. **Beim Start verwenden** legt fest, welches Profil beim Spielstart geladen wird.

## Tasten

| Taste | Aktion |
|---|---|
| Entf | Menü öffnen und schließen |
| + | Effekte an/aus |
| , und . | Vorheriges / nächstes Profil |
| - | A/B-Vergleich (links das Originalbild) |

Ändere sie im Menü mit **Tasten anpassen**, auch Lenkradtasten. Die Sprache wechselst du mit der Sprachtaste oben im Menü.

## Updates

Beim Spielstart prüft das Add-on auf GitHub, ob es eine neue Version gibt. Falls ja, zeigt das Menü **Aktualisieren**: Das Installationsprogramm wird heruntergeladen, mit seiner SHA-256-Prüfsumme geprüft und beim Beenden von rFactor 2 installiert. Die Prüfung sendet an GitHub nur den Namen und die Version des Programms.

Zum Kompilieren oder für Übersetzungen siehe den Abschnitt *Development* im [englischen README](README.md#development).

## Lizenz

Kostenlos, alle Rechte vorbehalten: siehe [LICENSE.txt](LICENSE.txt). ReShade steht unter der BSD-3-Clause-Lizenz, die Schrift Barlow Condensed unter der SIL Open Font License.
