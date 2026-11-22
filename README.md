# KarmaTV PostFX for rFactor2

**English** · [Italiano](README.it.md) · [Español](README.es.md) · [Français](README.fr.md) · [Deutsch](README.de.md) · [Português](README.pt.md)

Graphics effects for rFactor 2 with an in-game menu: ready-made profiles, advanced settings, telemetry-driven automation (rain, replays, camera) and automatic updates. It is a [ReShade](https://reshade.me) add-on; the installer sets ReShade up for you. The menu is available in English, Italian, Spanish, French, German and Portuguese.

## Install

1. Download `KarmaTV-PostFX-Setup-x.y.z.exe` from the [latest release](https://github.com/KarmaTV80/karmatv-postfx-rf2/releases/latest).
2. Close rFactor 2 and run the installer: it finds the game in your Steam libraries.
3. Start rF2, go on track and press **Delete** to open the menu.

Windows may show "Windows protected your PC" because the installer isn't digitally signed: choose **More info**, then **Run anyway**.

Automation needs [rF2SharedMemoryMapPlugin](https://github.com/TheIronWolfModding/rF2SharedMemoryMapPlugin), enabled in rF2's Plugins options. The effects work without it.

To uninstall: **Windows Settings > Apps > Installed apps > KarmaTV PostFX for rFactor2**. Your profiles stay unless you choose to delete them.

## Profiles

| Profile | What it's for |
|---|---|
| rF2 Original | No effects: the game's own graphics |
| Karma FX | Driving on a monitor |
| Replay Karma FX | Replays and broadcast: depth of field, motion blur. Switches on by itself in replays |
| VR Karma FX | Headset, with VR-friendly effects |

Built-in profiles can be changed from the **Advanced** button, but changes are saved only as a new profile. **Use at startup** picks the profile loaded when the game starts.

## Keys

| Key | Action |
|---|---|
| Delete | Open and close the menu |
| + | Effects on/off |
| , and . | Previous / next profile |
| - | A/B compare (original image on the left) |

Change them from the menu with **Customize keys**, wheel buttons included. The language button in the menu header switches the language.

## Updates

When the game starts, the add-on checks GitHub for a new version. If there is one, the menu shows **Update**: the installer is downloaded, checked against its SHA-256 checksum and installed when you close rFactor 2. The check sends GitHub only the program's name and version.

---

## Development

Requirements: Visual Studio 2022 (or Build Tools) with "Desktop development with C++", CMake 3.24+, Git, Python 3. For the installer: [Inno Setup 6](https://jrsoftware.org/isinfo.php).

```powershell
cmake --preset x64
cmake --build --preset release
```

If git refuses to clone ReShade because of "dubious ownership" (drives without file ownership):

```powershell
$env:GIT_CONFIG_COUNT="1"; $env:GIT_CONFIG_KEY_0="safe.directory"; $env:GIT_CONFIG_VALUE_0="*"
```

| Script | What it does |
|---|---|
| `tools\install.ps1 -Rf2Bin64 "<rF2>\Bin64"` | Installs the local build into rF2 (ReShade, add-on, shaders, LUTs, font) |
| `tools\uninstall.ps1 -Rf2Bin64 "<rF2>\Bin64"` | Removes what `install.ps1` installed |
| `tools\package.ps1` | Builds `dist\KarmaTV-PostFX-Setup-<version>.exe` and its `.sha256.txt` checksum |
| `python tools\i18n_check.py` | Lists menu texts still missing from `lang\*.tsv` |

**Translations.** The source text is Italian and works as the key: each `lang\<code>.tsv` holds lines `italian<TAB>translation`. Wrap new menu texts in `tr("...")`, then run `tools\i18n_check.py` until nothing is missing.

**Releasing.** Bump `VERSION` in `CMakeLists.txt`, run `tools\package.ps1`, then create a GitHub release tagged `v<version>` with both files from `dist` (`.exe` and `.exe.sha256.txt`). The add-on looks for exactly these two files.

```text
src/
  addon_main.cpp   add-on entry point and ReShade events
  app.*            global state, profiles, per-frame update
  core/            effect catalog, .ini presets, translations, log
  telemetry/       rF2 shared memory (scoring, camera)
  rules/           telemetry-driven automation
  input/           hotkeys and wheel buttons
  perf/            GPU profiler, spikes
  render/          effect application, depth, hook before the HUD
  ui/              in-game menu, theme, logo
  update/          update check and install
lang/              menu translations
shaders/           KarmaTV_<order>_<Effect>.fx
tools/             install, packaging, installer, LUT maker
```

## License

Free to use, all rights reserved: see [LICENSE.txt](LICENSE.txt). ReShade is distributed under the BSD-3-Clause license, the Barlow Condensed font under the SIL Open Font License.
