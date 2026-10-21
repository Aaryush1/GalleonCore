<p align="center"><img src="site/logo.svg" alt="" width="96" height="96"></p>

# GalleonCore

[![Website](https://img.shields.io/badge/website-GalleonCore-46505E)](https://paullux.github.io/GalleonCore/)
[![CI](https://github.com/Paullux/GalleonCore/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Paullux/GalleonCore/actions/workflows/ci.yml)
[![Linux packages](https://github.com/Paullux/GalleonCore/actions/workflows/linux-packages.yml/badge.svg?branch=main)](https://github.com/Paullux/GalleonCore/actions/workflows/linux-packages.yml)
[![Windows desktop](https://github.com/Paullux/GalleonCore/actions/workflows/windows-core.yml/badge.svg?branch=main)](https://github.com/Paullux/GalleonCore/actions/workflows/windows-core.yml)
[![Release](https://img.shields.io/github/v/release/Paullux/GalleonCore)](https://github.com/Paullux/GalleonCore/releases)
[![GitHub downloads](https://img.shields.io/github/downloads/Paullux/GalleonCore/total?label=GitHub%20downloads)](https://github.com/Paullux/GalleonCore/releases)
[![Ubuntu PPA](https://img.shields.io/badge/Ubuntu-PPA-E95420?logo=ubuntu&logoColor=white)](https://launchpad.net/~paulwoisard/+archive/ubuntu/galleoncore)
[![Fedora COPR](https://img.shields.io/badge/Fedora-COPR-51A2DA?logo=fedora&logoColor=white)](https://copr.fedorainfracloud.org/coprs/paullux/GalleonCore/)
[![Arch AUR](https://img.shields.io/badge/Arch-AUR-1793D1?logo=archlinux&logoColor=white)](https://aur.archlinux.org/packages/galleoncore)
[![Linux AppImage](https://img.shields.io/badge/Linux-AppImage-8E6BBF?logo=linux&logoColor=white)](https://github.com/Paullux/GalleonCore/releases/download/v0.1.0/GalleonCore-0.1.0-x86_64.AppImage)
[![Licenses](https://img.shields.io/badge/license-GPL%20%2F%20LGPL%20%2F%20CC--BY--SA-blue)](LICENSE.md)
[![Security policy](https://img.shields.io/badge/security-report%20privately-46505E)](SECURITY.md)

🇬🇧 [Read in English](README.md) · *La version anglaise fait référence en cas de différence.*

🌐 [Site web](https://paullux.github.io/GalleonCore/)

**La plateforme ouverte pour le Corsair Galleon 100 SD sous Linux.**

Le Galleon 100 SD est un clavier mécanique full-size dont le pavé numérique est remplacé par un module Elgato Stream Deck : 12 touches LCD, un écran et deux molettes. Corsair et Elgato ne supportent la partie Stream Deck que sous Windows et macOS. GalleonCore vise à rendre chaque partie du clavier utilisable sous Linux, et à l'intégrer au bureau.

> **En développement actif.** Les touches, l’écran et les molettes du Stream Deck fonctionnent sous Linux. Galleon Bosun permet de configurer le module. Les widgets Plasma restent prévus. Une préversion Windows avec Setup NSIS aux couleurs de Bosun et archive ZIP se prépare pour les essais interactifs ([détails](docs/WINDOWS.md)) ; consulte la [feuille de route](ROADMAP.md) pour les validations détaillées.

> GalleonCore n'est affilié ni à Corsair ni à Elgato.

## Objectifs

- **Module Stream Deck** : afficher sur les 12 touches et l'écran, lire les touches et les molettes.
- **Widgets partagés** : les mêmes widgets QML (horloge, moniteur système, lecture en cours…) rendus sur l'écran du clavier *et* affichés en widgets KDE Plasma sur le bureau.
- **Éclairage RVB** : RVB par touche, barre d'ambiance et anneaux des molettes, via [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB).
- **Plugin OpenRGB** : configurer le deck depuis OpenRGB et synchroniser ses thèmes avec l'éclairage.
- **Préversion Windows**, avec le même cœur et des backends natifs pour les médias, le son et le clavier ; validation matérielle encore attendue.

## État

| Partie | État |
|---|---|
| Protocole Stream Deck (touches, écran, molettes) | Keep-alive, luminosité, images des touches et de l'écran implémentés d'après le descripteur du module et le travail communautaire ; image de l'écran, images des touches, touches et molettes **confirmées sur le matériel** |
| Rendu QML hors écran vers le deck | Fond d'écran avec l'heure (`WallpaperClock.qml`), rendu logiciel |
| Démon + interface D-Bus | Choix du fond (`galleond --preview`) et `org.galleoncore.Deck` (`SetWallpaper`, `ClearWallpaper`, `Wallpapers`) |
| Pages de boutons (JSON) et lecteur de musique (MPRIS) | Implémentés, tests verts ; **touches, molettes, écran « en cours de lecture » (vraie pochette, titre, artiste, durée) et une touche d'application avec son icône confirmés sur le module** (rechargement à chaud, double appui, images de vidéos et fenêtre des applications : pas encore rapportés) |
| Widgets KDE Plasma | Squelette |
| Éclairage RVB | **En cours d'investigation.** Le protocole (Corsair « V2 »/Bragi) n'est encore supporté nulle part. Le support doit être intégré en amont dans OpenRGB. |
| Plugin OpenRGB | Anneaux des molettes implémentés dans le plugin séparé ; RVB du clavier toujours en investigation |
| Windows | Préversion desktop ; CI native et essais interactifs suivis dans [WINDOWS.md](docs/WINDOWS.md) |

### Captures de Galleon Bosun

Les cinq pages par défaut montrent les applications installées sur un poste Linux ; les applications et leurs icônes varient selon l’ordinateur. Les deux dernières captures montrent le choix des fonds d’écran et des vidéos de veille. [Voir la galerie sur le site](https://paullux.github.io/GalleonCore/#previews).

| Contrôle | Multimédia |
|---|---|
| ![Page Contrôle de Bosun avec les touches multimédias](site/screenshots/control.png) | ![Page Multimédia de Bosun avec navigateurs et lecteurs](site/screenshots/multimedia.png) |
| Jeu vidéo | Système |
| ![Page Jeu Vidéo de Bosun avec les jeux installés](site/screenshots/games.png) | ![Page Système de Bosun avec les outils utiles](site/screenshots/system.png) |
| Pavé numérique | Choix du fond d’écran |
| ![Pavé numérique dans Bosun](site/screenshots/numpad.png) | ![Catalogue de fonds d’écran dans Bosun](site/screenshots/wallpapers.png) |
| Choix de la veille | |
| ![Choix des deux vidéos de veille du dépôt dans Bosun](site/screenshots/screensavers.png) | |

### Windows : les touches du Stream Deck ne répondent pas

Sur le Galleon 100 SD du propriétaire, les images se mettaient à jour mais les
touches LCD et les molettes ne répondaient qu'après une réinitialisation du
module. La mise à jour du firmware Stream Deck avec l'application officielle
Elgato **sous Windows**, suivie de la désinstallation de cette application, a
rétabli les commandes lors de son essai suivant. Ces deux changements ayant été
faits ensemble, on ne sait pas lequel a suffi à résoudre le défaut sur d'autres
appareils.

Si tu rencontres le même symptôme, relève d'abord la version du firmware. Ferme
GalleonCore et les autres applications qui pilotent le module. Dans l'application
Elgato Stream Deck sous Windows ou macOS, ouvre **Préférences → Appareils →
Galleon 100 SD → Mettre à jour le microprogramme**. Laisse le clavier connecté
jusqu'à la fin de la mise à jour. Note la nouvelle version, ferme Stream Deck
et teste GalleonCore après un démarrage à froid. Si Stream Deck ne te sert que
pour ce clavier, tu peux le
désinstaller après la mise à jour ; garde-le si un autre périphérique en dépend.
La capture du propriétaire affiche la version `3.06.005`, également citée dans
les [notes d'Elgato pour la version 7.4.1](https://edge.elgato.com/egc/sd_content/releasenotes/7.4.1.html).
La procédure d'essai Windows et ses limites sont dans [WINDOWS.md](docs/WINDOWS.md).

Le firmware du **clavier principal** est distinct : Corsair indique de passer
par [Corsair Web Hub](https://www.corsair.com/web-hub) dans un navigateur
compatible, rubrique **Paramètres de l'appareil**. Il n'y a aucune application
à installer ni à désinstaller. Le GIF ci-dessous montre la mise à jour du
*module Stream Deck* dans l'application Elgato, pas celle du clavier.

![Préférences Appareils de Stream Deck montrant le bouton de mise à jour du firmware Galleon](site/firmware-update.gif)

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
openrgb-plugin/    Plugin OpenRGB pour les anneaux des molettes
```

Garder le protocole sans entrées/sorties permet de tester unitairement et de fuzzer chaque format de trame sans le clavier.

Le contrôleur RVB lui-même ne vit pas ici : il sera développé dans un fork d'OpenRGB et proposé en merge request au `CorsairPeripheralV2Controller`, pour que tous les utilisateurs d'OpenRGB en profitent.

## Compilation

Facultatif : `ffmpeg` (images des vidéos) et `gio` (lancer les applications).

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

Les 12 touches et les 2 molettes exécutent les actions d'un fichier JSON, `~/.config/galleoncore/pages.json` (ou `--pages fichier`). Sur un profil neuf, sous Linux comme sous Windows, les cinq onglets intégrés sont Contrôle, Multimédia, Jeu Vidéo, Système et Pavé Numérique. Multimédia privilégie les navigateurs et lecteurs, Système propose des outils d'administration et Jeu Vidéo commence par les jeux Steam installés. Les suggestions sont choisies une seule fois, avec au plus 12 touches par page ; un lanceur absent ou sans rapport laisse sa place vide. Supprimer ou modifier une touche dans Bosun ne la remplit ni ne la déplace ensuite. Les pages personnelles existantes restent intactes. Le fichier est relu quand il change ; un fichier avec une erreur est refusé en entier et les pages précédentes restent (les erreurs sont dans le journal).

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
- Actions : `media` (`play-pause`, `next`, `previous`, `stop`, `shuffle` (aléatoire), `repeat` (aucune, liste, piste), `mute` (le son du système, pour tous les lecteurs), `raise` (amener la fenêtre du lecteur devant), `fullscreen` (un lecteur vidéo), `restart` (retour au début de la piste), `volume` avec `step` en pourcent, `seek` avec `seconds`). Une touche `shuffle` ou `repeat` affiche son état : ajoutez `"state_labels": ["NON", "OUI"]` (aléatoire) ou `["NON", "LISTE", "PISTE"]` (répétition) pour choisir les mots ; la touche est plus claire quand la fonction est active, `page` (ouvrir une page), `back`, `app` (lancer une application, voir plus bas), `none`. Le `turn` d'une molette prend `volume` ou `seek`, appliqué à chaque cran.
- Le lecteur de musique est trouvé par MPRIS : celui qui joue, sinon le dernier qui a joué.
- Les actions viennent de ce fichier seulement. La seule façon de lancer un programme est une touche `app`, qui désigne un fichier `.desktop` : aucune commande ne peut s'écrire dans le fichier.
- `galleond --render-keys touches.png` dessine les touches de la première page dans une image, sans le matériel.

### Applications sur les touches

Une touche peut lancer une application de ton bureau, sans commande à taper : elle désigne le fichier `.desktop` de l'application, dont le nom et l'icône servent à la touche, sauf si tu donnes les tiens.

```json
{ "key": 4, "action": { "type": "app", "desktop": "firefox.desktop" } }
```

`desktop` est un identifiant de fichier (`firefox.desktop`, `org.kde.dolphin.desktop`) ou le chemin absolu d'un fichier `.desktop`. C'est le lanceur du bureau qui la démarre (`gio launch`, sinon `gtk-launch`, sinon `kioclient`) : `Exec`, les arguments et `Terminal=true` sont traités comme le fait le bureau. Un double appui ne la lance qu'une fois. Par sécurité, le fichier de pages doit t'appartenir et ne pas être modifiable par les autres, sinon les touches d'application sont désactivées.

Tu n'as jamais besoin de modifier le JSON pour cela :

```sh
./build/daemon/galleond --apps                              # une fenêtre : choisir une application, choisir une touche
./build/daemon/galleond --list-apps --filter vlc            # les applications installées
./build/daemon/galleond --assign-app vlc.desktop --key 4    # en mettre une sur une touche (--page NOM pour une autre page)
./build/daemon/galleond --clear-key 4
```

**Galleon Bosun** (en cours de construction) est la fenêtre de réglage : un onglet par thème avec les 12 touches dessinées comme sur le module, et des onglets pour l'éclairage du clavier, les fonds d'écran et les écrans de veille (pas encore faits). Elle montre seulement les touches pour l'instant ; l'édition vient ensuite.

```sh
./build/bosun/galleon-bosun
```

La modification est vérifiée comme tout fichier de pages, l'ancienne version est gardée dans `pages.json.bak`, et un `galleond` en marche relit le changement tout seul.

### Thèmes

Une liste `themes` donne un nom et une couleur à des pages. Un appui de molette avec `{ "type": "theme" }` fait passer cette molette au choix du thème : la tourner parcourt les thèmes, un second appui revient au contrôle du lecteur (après 30 secondes sans rotation, le retour se fait tout seul). Les touches montrent la page du thème, une ligne de la couleur du thème court en haut de l'écran avec son nom, et l'anneau de la molette gauche prend la couleur (l'anneau droit reste blanc, pour le contrôle du lecteur). Pendant le choix du thème, la ligne du bas de l'écran prend aussi la couleur du thème. Sur une touche, la même action passe au thème suivant.

```json
"themes": [
  { "name": "Contrôle", "page": "main", "color": "#ffffff" },
  { "name": "Son", "page": "son", "color": "#e53935" }
],
"dials": { "0": { "press": { "type": "theme" } } }
```

La couleur des anneaux utilise le rapport de fonctionnalité par LED du protocole Stream Deck (`03 24 <led> <r> <v> <b>`, tiré de la PR 63 de rust-elgato-streamdeck). Il n'est pas encore vérifié sur le module. 8 thèmes au plus ; la couleur est `#rrggbb` ; la page d'un thème doit exister.

### Choisir le fond d'écran avec une molette

Les fonds sont classés par style, puis par nom de fichier en lisant les nombres comme des nombres (`-2` avant `-10`) ; la molette avance d'un fond à la fois, et les rotations sont ignorées, pas mises en file, tant que le fond du dernier cran n'est pas réellement à l'écran, plus un repos de `screen.wallpaper_step_delay_ms` (150 par défaut ; 0 : aucune règle, chaque cran compte), et l'écran montre le nom et la place du fond en cours de choix (`portrait-01   10/24`).

Un appui de molette avec `{ "type": "wallpaper-mode" }` fait passer cette molette du contrôle du lecteur au choix du fond d'écran : la tourner parcourt les fonds du catalogue (le choix est retenu, et le fond s'affiche même pendant qu'une piste joue), un second appui valide et revient en arrière. Les rotations sont ignorées tant que l'écran n'affiche pas le nom et la place du fond. Après la dernière rotation, un décompte de `screen.wallpaper_validate_seconds` (4 par défaut) s'affiche dans un cercle pendant que la molette reste dans le choix (anneau magenta, une rotation le relance) ; à sa fin le choix est validé : la molette redevient le contrôle du lecteur (anneau et ligne blancs) et le fond choisi reste à l'écran pendant `screen.wallpaper_hold_seconds` secondes avant qu'une piste en cours reprenne la main. Une fine ligne en bas de l'écran et l'anneau de la molette droite indiquent le mode : blanc pour le contrôle du lecteur, magenta pour le choix du fond.

### Thème pavé numérique, thèmes automatiques et touches teintées

Une action `{ "type": "key", "key": "kp7" }` appuie sur une touche de clavier par l'intermédiaire d'un clavier virtuel (`/dev/uinput`), maintenue tant que la touche est maintenue. Seule une liste fixe de noms est acceptée (`kp0` à `kp9`, `kpdot`, `kpenter`, `kpplus`, `kpminus`, `kpasterisk`, `kpslash`, `numlock`, `backspace`), et le périphérique virtuel ne reçoit aucune autre touche. Le verrouillage numérique doit être actif pour que les chiffres du pavé écrivent des chiffres. Le périphérique virtuel demande l'accès en écriture à `/dev/uinput` (`tools/udev/71-galleon-uinput.rules`).

Un thème peut se choisir tout seul avec `"when": "media"` (un lecteur démarre) ou `"when": "no-media"` (il n'y en a plus : arrêté ou absent ; un lecteur en pause compte encore). Seulement à ces moments : votre choix à la molette reste jusqu'au changement suivant. Un thème au plus par déclencheur. Les touches de la page d'un thème sont dessinées sur une teinte plus sombre de sa couleur, plus claire pendant l'appui.

La section `screen` a aussi `wallpaper_hold_seconds` (de 1 à 600, 4 par défaut) : combien de temps un fond choisi à la molette droite, ou affiché au démarrage, reste à l'écran avant qu'une piste en cours reprenne la main.

### Applications et jeux Steam les plus utilisés

Une page peut demander que ses touches d'application soient classées par usage avec `"sort": "usage"` : les mêmes places, l'application la plus utilisée dans la première, marquée d'une petite étoile dorée. Chaque lancement depuis le module compte 1 et perd la moitié de sa valeur tous les 30 jours : une habitude récente l'emporte sur une ancienne. Les comptes sont gardés dans `~/.local/share/galleoncore/usage.json` (200 applications au plus, écriture atomique).

```json
"jeux": {
  "sort": "usage",
  "steam": { "max": 6 },
  "keys": [ { "key": 0, "action": { "type": "app", "desktop": "steam.desktop" } } ]
}
```

`"menu": { "category": "Game", "max": N }` remplit les touches libres de la page avec jusqu'à N applications de cette catégorie du menu du bureau (le champ `Categories` de chaque fichier `.desktop` ; `Game` pour les jeux du menu KDE, quel que soit leur lanceur : jeux KDE, Lutris, Heroic, Wine, raccourcis Steam...), les plus utilisées d'abord. Un raccourci du menu qui lance un jeu Steam (`steam steam://rungameid/<n>`) est classé d'après le temps de jeu et le dernier lancement de ce jeu dans les données locales de Steam, même si le jeu n'est pas installé ici. Les outils (Proton, les runtimes Steam) et ce qui est déjà sur la page sont écartés, et un raccourci de jeu Steam et le même jeu de la liste de Steam ne sont montrés qu'une fois.

`"steam": { "max": N }` remplit les touches libres de la page avec jusqu'à N jeux Steam installés, les plus joués d'abord. Il lit les fichiers de Steam sur cette machine, en lecture seule, sans réseau, sans compte ni clé : le temps de jeu et la date du dernier lancement de chaque jeu (`userdata/<id>/config/localconfig.vdf`) et le nom de chaque jeu installé (`appmanifest_*.acf` dans les dossiers de bibliothèque). Les outils comme Proton et les runtimes Steam sont écartés. Un jeu Steam a pour score ses heures de jeu, réduites selon le temps écoulé depuis son dernier lancement, plus ses lancements depuis le module. Un jeu peut aussi avoir sa propre touche : `{ "type": "steam", "appid": 730 }` le lance par `steam steam://rungameid/730`.

### Écran de veille animé

Après un temps sans utilisation, une vidéo verticale peut défiler sur l'écran et les 12 touches ensemble, découpée comme si l'image était vue à travers elles (les espaces entre les touches sont laissés de côté). Une touche, une molette, ou le début d'une piste (sauf si `while_playing` vaut true) ramène l'écran normal ; l'appui qui réveille le module ne fait rien d'autre.

```json
"screensaver": {
  "video": "veille.mp4",
  "idle_minutes": 5,
  "fps": 15,
  "while_playing": false,
  "geometry": {
    "screen_mm": [L, H],
    "key_mm": [L, H],
    "gap_mm": [X, Y],
    "screen_to_keys_mm": D
  }
}
```

Les tailles sont celles du module, en millimètres, mesurées à la règle sur le verre visible (l'écran, une touche, les espaces entre touches, et la distance du bas de l'écran au haut de la première rangée de touches). Elles ne sont jamais devinées : sans `geometry`, la veille reste éteinte. Le module est supposé centré : l'écran et la grille de touches partagent le même axe vertical. La vidéo est mise à l'échelle pour couvrir l'image (`720` pixels de large, aussi haute que le module en proportion) et jouée en boucle ; donnez-lui cette forme, par exemple `720 x 1100`, pour que rien ne soit rogné. Pour une boucle sans coupure, finissez-la sur sa première image. Le chemin de la vidéo est absolu, `~/...`, ou relatif au fichier de pages. `"video"` peut aussi être une liste (jusqu'à 8), chaque entrée étant un chemin ou `{ "file": "...", "title": "Nom d'affiche" }` : les vidéos passent à tour de rôle, une par démarrage de la veille, et le titre est écrit dans le journal. FFmpeg la décode dans son propre processus (sans shell, priorité minimale, sans audio, protocole `file` seulement) ; le module accepte environ 20 tours écran + touches par seconde (mesuré), donc `fps` va jusqu'à 20 et 15 est confortable.

Les vidéos que vous mettez sur votre propre clavier restent sur votre ordinateur. Une vidéo ajoutée à ce dépôt suit [ASSETS.md](ASSETS.md).

### En cours de lecture sur l'écran

Quand un lecteur joue, l'écran affiche le morceau (pochette, titre, artiste, temps et barre de progression) à la place du fond d'écran, et revient au fond quand la musique s'arrête. Une section `screen` facultative du fichier de pages le règle :

```json
"screen": { "now_playing": "playing", "remote_covers": true, "video_thumbnails": true }
```

- `now_playing` : `playing` (par défaut), `playing-or-paused` ou `never`.
- `video_thumbnails` : `false` pour ne jamais faire d'image d'une vidéo avec FFmpeg.
- `remote_covers` : `false` pour ne charger que les pochettes de fichiers locaux. Les pochettes en `https://` sont téléchargées par `galleond` ; il ne télécharge jamais depuis `http://`, depuis une adresse IP ni depuis un nom local, limite la taille, la durée et les redirections, et ne décode que du JPEG, du PNG et du WebP.
- La pochette est, dans l'ordre : celle que donne le lecteur, une image à côté du fichier lu (`cover.jpg`, `folder.png`, `front.jpg`, `albumart.jpg`, `album.jpg`, ou une image du même nom que le fichier), une image de la vidéo faite avec FFmpeg et gardée dans `~/.cache/galleoncore/thumbnails` (300 images ou 100 Mo au plus), l'icône du lecteur (le cône de VLC), puis une note. Un morceau sans titre affiche le nom de son fichier.
- L'écran suit le lecteur immédiatement : les changements de morceau et d'état arrivent par des notifications D-Bus, et seule la barre de progression est lue une fois par seconde.
- `galleond --render-now-playing ecran.png --wallpaper cinematic/cinematic-02.jpg` dessine un écran d'exemple sans le matériel.

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
- Application, widgets, outils et documentation : **GPL-3.0-or-later**.
- Fonds d’écran et vidéos du dépôt : **CC-BY-SA-4.0**, avec provenance (voir [ASSETS.md](ASSETS.md)).

Les textes complets sont dans [`LICENSES/`](LICENSES/). Les fichiers portent des en-têtes SPDX, selon la convention [REUSE](https://reuse.software).

## Paquets et communauté

Choisis ton système dans la [section téléchargements du site](https://paullux.github.io/GalleonCore/#downloads)
pour obtenir directement les paquets Windows, Ubuntu, Fedora, Arch et AppImage.
La [release v0.1.0](https://github.com/Paullux/GalleonCore/releases/tag/v0.1.0)
explique comment ajouter les dépôts PPA, COPR ou AUR et à quoi sert chaque fichier.
Les détails de construction figurent dans le [guide de distribution](tools/packaging/README.md).
Le compteur GitHub concerne uniquement les fichiers des releases, pas les installations PPA/COPR/AUR.

[Contribuer](CONTRIBUTING.md) · [Code de conduite](CODE_OF_CONDUCT.md) ·
[Sécurité](SECURITY.md) · [Licences](LICENSE.md).
