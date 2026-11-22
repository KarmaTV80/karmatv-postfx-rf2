# KarmaTV PostFX for rFactor2

[English](README.md) · [Italiano](README.it.md) · [Español](README.es.md) · **Français** · [Deutsch](README.de.md) · [Português](README.pt.md)

Des effets graphiques pour rFactor 2 avec un menu en jeu : profils prêts à l'emploi, réglages avancés, automatismes pilotés par la télémétrie (pluie, replays, caméra) et mises à jour automatiques. C'est un add-on [ReShade](https://reshade.me) ; l'installateur prépare ReShade pour vous. Le menu est en anglais, italien, espagnol, français, allemand et portugais.

## Installation

1. Téléchargez `KarmaTV-PostFX-Setup-x.y.z.exe` depuis la [dernière version](https://github.com/KarmaTV80/karmatv-postfx-rf2/releases/latest).
2. Fermez rFactor 2 et lancez l'installateur : il trouve le jeu dans vos bibliothèques Steam.
3. Lancez rF2, allez en piste et appuyez sur **Suppr** pour ouvrir le menu.

Windows peut afficher « Windows a protégé votre ordinateur » car l'installateur n'a pas de signature numérique : choisissez **Informations complémentaires**, puis **Exécuter quand même**.

Les automatismes nécessitent [rF2SharedMemoryMapPlugin](https://github.com/TheIronWolfModding/rF2SharedMemoryMapPlugin), activé dans les options Plugins de rF2. Les effets fonctionnent sans lui.

Pour désinstaller : **Paramètres Windows > Applications > Applications installées > KarmaTV PostFX for rFactor2**. Vos profils sont conservés, sauf si vous choisissez de les supprimer.

## Profils

| Profil | Usage |
|---|---|
| rF2 d'origine | Aucun effet : les graphismes du jeu |
| Karma FX | Conduite sur écran |
| Replay Karma FX | Replays et réalisation : profondeur de champ, flou de mouvement. S'active tout seul dans les replays |
| VR Karma FX | Casque, avec des effets adaptés à la VR |

Les profils fixes se modifient avec le bouton **Avancé**, mais les modifications ne s'enregistrent que comme nouveau profil. **Utiliser au démarrage** choisit le profil chargé à l'ouverture du jeu.

## Touches

| Touche | Action |
|---|---|
| Suppr | Ouvre et ferme le menu |
| + | Effets on/off |
| , et . | Profil précédent / suivant |
| - | Comparaison A/B (image d'origine à gauche) |

Modifiez-les depuis le menu avec **Personnaliser les touches**, boutons du volant compris. Le bouton de langue dans l'en-tête du menu change la langue.

## Mises à jour

Au démarrage du jeu, l'add-on vérifie sur GitHub s'il existe une nouvelle version. Si oui, le menu affiche **Mettre à jour** : l'installateur est téléchargé, vérifié avec son empreinte SHA-256 et installé à la fermeture de rFactor 2. La vérification n'envoie à GitHub que le nom et la version du programme.

Pour compiler ou aider aux traductions, voir la section *Development* du [README anglais](README.md#development).

## Licence

Gratuit, tous droits réservés : voir [LICENSE.txt](LICENSE.txt). ReShade est distribué sous licence BSD-3-Clause, la police Barlow Condensed sous SIL Open Font License.
