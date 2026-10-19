<p align="center"><img src="site/logo.svg" alt="" width="96" height="96"></p>

# GalleonCore

🇬🇧 [Read in English](README.md) · *La version anglaise fait référence en cas de différence.*

🌐 [Site web](https://paullux.github.io/GalleonCore/)

**La plateforme ouverte pour le Corsair Galleon 100 SD sous Linux.**

Le Galleon 100 SD est un clavier mécanique full-size dont le pavé numérique est remplacé par un module Elgato Stream Deck : 12 touches LCD, un écran et deux molettes. Corsair et Elgato ne supportent la partie Stream Deck que sous Windows et macOS. GalleonCore vise à rendre chaque partie du clavier utilisable sous Linux, et à l'intégrer au bureau.

> ⚠️ **Projet à ses débuts.** Le dépôt contient la structure du projet, le squelette de compilation et l'outillage de tests. Rien ne communique encore avec le matériel.

> GalleonCore n'est affilié ni à Corsair ni à Elgato.

## Objectifs

- **Module Stream Deck** : afficher sur les 12 touches et l'écran, lire les touches et les molettes.
- **Widgets partagés** : les mêmes widgets QML (horloge, moniteur système, lecture en cours…) rendus sur l'écran du clavier *et* affichés en widgets KDE Plasma sur le bureau.
- **Éclairage RVB** : RVB par touche, barre d'ambiance et anneaux des molettes, via [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB).
- **Plugin OpenRGB** : configurer le deck depuis OpenRGB et synchroniser ses thèmes avec l'éclairage.
- **Portage Windows**, plus tard, pour tout ce qui n'est pas spécifique à KDE.

## État

| Partie | État |
|---|---|
| Protocole Stream Deck (touches, écran, molettes) | Keep-alive, luminosité, images des touches et de l'écran implémentés d'après le descripteur du module et le travail communautaire ; image de l'écran, images des touches, touches et molettes **confirmées sur le matériel** |
| Rendu QML hors écran vers le deck | Fond d'écran avec l'heure (`WallpaperClock.qml`), rendu logiciel |
| Démon + interface D-Bus | Choix du fond (`galleond --preview`) et `org.galleoncore.Deck` (`SetWallpaper`, `ClearWallpaper`, `Wallpapers`) |
| Pages de boutons (JSON) et lecteur de musique (MPRIS) | Implémentés, tests verts, **pas encore essayés sur le module** |
| Widgets KDE Plasma | Squelette |
| Éclairage RVB | **En cours d'investigation.** Le protocole (Corsair « V2 »/Bragi) n'est encore supporté nulle part. Le support doit être intégré en amont dans OpenRGB. |
| Plugin OpenRGB | Prévu |
| Windows | Prévu, en dernier |

## Architecture

```
lib/               libgalleon, bibliothèque C++20/Qt 6 (LGPL-3.0-or-later)
  Protocol         fonctions pures : octets ↔ événements et rapports, sans E/S
  Transport        E/S HID brutes (hidapi), remplaçables par un faux en test
  StreamDeck       keep-alive, lecture des entrées, signaux Qt
daemon/            galleond : pilote le deck, expose une API D-Bus
widgets/           Widgets QML partagés, sans dépendance à Plasma
plasmoid/          Widgets KDE Plasma réutilisant widgets/
tests/             Tests unitaires Qt Test
fuzz/              Cibles libFuzzer pour les parseurs du protocole
openrgb-plugin/    (prévu) Plugin OpenRGB
```

Garder le protocole sans entrées/sorties permet de tester unitairement et de fuzzer chaque format de trame sans le clavier.

Le contrôleur RVB lui-même ne vit pas ici : il sera développé dans un fork d'OpenRGB et proposé en merge request au `CorsairPeripheralV2Controller`, pour que tous les utilisateurs d'OpenRGB en profitent.

## Compilation

Prérequis : CMake ≥ 3.21, un compilateur C++20 (GCC ≥ 11 ou Clang ≥ 14), Qt 6 (Core, Gui, Qml, Quick, DBus), hidapi (backend hidraw).

Sous Debian/Ubuntu :

```sh
sudo apt install cmake g++ qt6-base-dev qt6-declarative-dev libhidapi-dev pkg-config
cmake -B build
cmake --build build
ctest --test-dir build
```

Options CMake :

| Option | Défaut | Effet |
|---|---|---|
| `BUILD_TESTING` | `ON` | Compile les tests unitaires |
| `GALLEON_WERROR` | `OFF` | Traite les avertissements comme des erreurs |
| `GALLEON_SANITIZE` | `OFF` | AddressSanitizer + UndefinedBehaviorSanitizer |
| `GALLEON_FUZZ` | `OFF` | Cibles libFuzzer (Clang uniquement) |

Pour donner à ton utilisateur l'accès au clavier sans root :

```sh
sudo cp tools/udev/70-galleon.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

## Qualité du code

À chaque push et pull request, GitHub Actions lance :

- une compilation avec avertissements stricts traités en erreurs, avec GCC et avec Clang ;
- les tests unitaires, y compris sous AddressSanitizer et UndefinedBehaviorSanitizer ;
- 60 secondes de fuzzing sur le parseur des rapports d'entrée ;
- clang-tidy, qmllint, et `reuse lint` pour les licences.

Pour reproduire la passe avec sanitizers en local :

```sh
CXX=clang++ cmake -B build-asan -DGALLEON_SANITIZE=ON -DGALLEON_FUZZ=ON
cmake --build build-asan
ctest --test-dir build-asan
./build-asan/fuzz/fuzz_input_report -max_total_time=60
```

## Fond d'écran et heure sur l'écran

`galleond` dessine une des images de [`public/wallpaper/`](public/wallpaper/) sur l'écran du Galleon (720 × 384), avec l'heure par-dessus, et la redessine chaque minute.

```sh
./build/daemon/galleond --preview                         # fenêtre de choix du fond
./build/daemon/galleond --wallpaper manga-cyberpunk/manga-cyberpunk-01.jpg
./build/daemon/galleond --wallpaper cinematic/cinematic-02.jpg --render-to ecran.png   # sans matériel
busctl --user call org.galleoncore.Deck /org/galleoncore/Deck org.galleoncore.Deck SetWallpaper s cinematic/cinematic-02.jpg
```

Seules les images du dossier des fonds peuvent être choisies, et chacune est vérifiée (vrai JPEG ou PNG, taille, pas de lien symbolique). Le dernier choix est mémorisé. Le dossier est `--wallpapers`, `$GALLEONCORE_WALLPAPERS`, `<dossier de données>/galleoncore/wallpaper`, ou l'arbre des sources.

## Pages de boutons

Les 12 touches et les 2 molettes exécutent les actions d'un fichier JSON, `~/.config/galleoncore/pages.json` (ou `--pages fichier`). Sans lui, des pages intégrées pilotent le lecteur de musique. Le fichier est relu quand il change ; un fichier avec une erreur est refusé en entier et les pages précédentes restent (les erreurs sont dans le journal).

```json
{
  "version": 1,
  "home": "main",
  "pages": {
    "main": { "keys": [
      { "key": 0, "label": "Précédent", "icon": "media-skip-backward", "action": { "type": "media", "command": "previous" } },
      { "key": 1, "label": "Lecture", "icon": { "playing": "media-playback-pause", "paused": "media-playback-start" },
        "action": { "type": "media", "command": "play-pause" } },
      { "key": 11, "label": "Plus", "action": { "type": "page", "page": "other" } }
    ] },
    "other": { "keys": [ { "key": 0, "label": "Retour", "action": { "type": "back" } } ] }
  },
  "dials": { "0": { "turn": { "type": "media", "command": "volume", "step": 2 }, "press": { "type": "media", "command": "play-pause" } } }
}
```

- Les touches sont numérotées de 0 à 11, rangée par rangée depuis le haut gauche. Une touche sans `action` ne fait rien.
- `icon` : un nom du thème d'icônes du bureau, ou un fichier à côté du fichier de pages (`.png`, `.jpg`, `.svg` ; pas de chemin). Elle peut être un objet avec un nom par état du lecteur (`playing`, `paused`, `stopped`).
- Actions : `media` (`play-pause`, `next`, `previous`, `stop`, `volume` avec `step` en pourcent, `seek` avec `seconds`), `page` (ouvrir une page), `back`, `none`. Le `turn` d'une molette prend `volume` ou `seek`, appliqué à chaque cran.
- Le lecteur de musique est trouvé par MPRIS : celui qui joue, sinon le dernier qui a joué.
- Les actions viennent de ce fichier seulement ; rien ne lance de programme.
- `galleond --render-keys touches.png` dessine les touches de la première page dans une image, sans le matériel.

## Notes matérielles

| Partie | ID USB |
|---|---|
| Clavier (RVB, touches) | `1b1c:2b0c` |
| Module Stream Deck | `1b1c:2b18` |

Les notes de protocole et les captures vont dans [`docs/protocol/`](docs/protocol/). Les notes de conception détaillées sont dans [`docs/ROADMAP.md`](docs/ROADMAP.md). Les images et vidéos ajoutées au dépôt suivent [`ASSETS.md`](ASSETS.md) (en anglais).

## Remerciements

GalleonCore s'appuie sur la rétro-ingénierie réalisée par :

- [node-elgato-stream-deck](https://github.com/Julusian/node-elgato-stream-deck) (MIT), qui a ajouté le support du Galleon en v7.5.0 ;
- [galleon-deck](https://github.com/NLMP-DDHS/galleon-deck) (MIT), un service Stream Deck en Python pour le Galleon sous Linux ;
- [opendeck-galleon](https://github.com/shanelord01/opendeck-galleon), un plugin OpenDeck pour le Galleon.

## Licence

- `lib/` (libgalleon) : **LGPL-3.0-or-later**, pour que d'autres projets puissent réutiliser la bibliothèque de protocole.
- Tout le reste : **GPL-3.0-or-later**.

Les textes complets sont dans [`LICENSES/`](LICENSES/). Les fichiers portent des en-têtes SPDX, selon la convention [REUSE](https://reuse.software).
