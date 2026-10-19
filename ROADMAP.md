# ROADMAP

Référence pour reprendre le développement à tout moment, y compris dans une nouvelle session Claude Code. À mettre à jour après chaque fonctionnalité terminée **et** testée (voir « Règle de mise à jour » en bas). Les notes de conception détaillées (protocole, RGB, OpenRGB, Windows) sont dans [docs/ROADMAP.md](docs/ROADMAP.md) ; les règles de code dans [CLAUDE.md](CLAUDE.md).

État au 19 octobre 2026, branche `galleond-wallpaper-clock` (non fusionnée, rien de poussé).

## 1. Implémenté et testé

« Testé » = tests Qt automatiques (16 exécutables dans `tests/`, sous `-Werror`). « Sur le module » = confirmé par l'utilisateur sur le matériel (photo ou retour).

| Fonction | Tests | Sur le module |
|---|---|---|
| Fond d'écran + heure/date (locale du système) sur l'écran 720×384, redessiné chaque minute | oui | oui |
| Fond retenu (`galleond.conf`) et affiché dès le démarrage ; avertissement si le fond sauvegardé n'existe plus | oui | oui |
| Images des 12 touches (JPEG 160×160), dessin partiel (seule la touche changée est renvoyée) | oui | oui |
| Entrées : 12 touches, 2 molettes (rotation + appui), décodage validé et fuzzé | oui + fuzz | oui |
| Pages de boutons en JSON (`~/.config/galleoncore/pages.json`), validation stricte (champ inconnu = fichier refusé), rechargement à chaud | oui | oui (déplacement d'une touche pendant l'exécution) |
| Contrôle MPRIS : lecture/pause, suivant, précédent, stop, volume, avance/recul ; volume par pas cumulés | oui (faux lecteur sur le bus) | oui (VLC, Clementine) |
| Écran « en cours de lecture » : titre, artiste, chronologie, pochette ; mise à jour quasi instantanée au changement de piste (`PropertiesChanged`) | oui | oui (pochette réelle vue) |
| Pochettes : fichier local, https seulement (pas d'IP ni de nom local), vignette vidéo FFmpeg en cache, icône du lecteur en dernier recours | oui | partiel (pochette et icône vues ; vignette vidéo non confirmée) |
| Touches « application » : lancement d'un `.desktop` (`gio launch`, sans shell), icône et nom pris du `.desktop`, anti-rebond ; fichier de pages réservé à son propriétaire | oui | oui (Firefox, Dolphin) |
| Sélecteur d'applications (`--apps`, `--list-apps`, `--assign-app`, `--clear-key`) | oui | non confirmé |
| Thèmes : pages colorées (Contrôle blanc, Multimédia cyan, Son rouge, Jeux bleu marine, Système vert) ; clic molette gauche = mode choix du thème, rotation = thème suivant/précédent, second clic ou 30 s = retour au contrôle | oui | oui |
| Ligne de couleur et nom du thème en haut de l'écran ; bandeau `◀ Nom ▶` pendant le choix | oui | oui |
| Anneaux des molettes indépendants : gauche = couleur du thème, droite = blanc (contrôle) ou magenta (choix du fond) ; rapport `03 24` par LED | oui | oui (couleurs vues) |
| Choix du fond par la molette droite : clic = mode fond (anneau et barre du bas magenta), rotation = fond suivant/précédent, second clic ou 30 s = retour ; le fond s'affiche même si une piste joue | oui | oui (validé par l'utilisateur) |
| Un seul `galleond` à la fois : le nom D-Bus est pris avant d'ouvrir le module, un second démarrage s'arrête (code 1) sans toucher au module | manuel | à confirmer |
| Service systemd utilisateur (`tools/systemd/galleond.service`, variante `galleond-dev.service` pour l'arbre de compilation) | `systemd-analyze verify` | démarre et pilote le module (journal du 19 octobre) ; démarrage à l'ouverture de session et reprise après veille à confirmer |
| Écran de veille animé : vidéo verticale (FFmpeg → images brutes) découpée pour l'écran 720×384 et les 12 touches 160×160, boucle, délai d'inactivité, réveil par touche/molette/début de piste, suspension des autres contrôleurs ; section `screensaver` du JSON (géométrie en mm exigée) | oui (découpage, FFmpeg réel, réveil, médias) | non : en attente des mesures en mm et d'une vidéo |
| Débit du module mesuré (`--measure-throughput`) : écran 44,7 i/s, touches 35 tours/s, écran + touches 20 tours/s | — | oui (mesuré le 19 octobre) |
| Thème « Pavé numérique » (touches = clavier virtuel uinput, liste fixe de touches, touche maintenue tant qu'on appuie) choisi tout seul quand aucun média ne joue, Contrôle quand un lecteur joue (`"when"`) ; touches teintées d'une version sombre de la couleur du thème | oui | à confirmer (retour de l'utilisateur) |
| Plugin OpenRGB (LampArray en libusb) : anneaux des molettes pilotés depuis OpenRGB | oui | oui (les anneaux ; le clavier non, voir §4) |

## 2. En cours

- **Aperçu fluide** du choix du fond : chaque cran renvoie un JPEG de l'écran. À mesurer sur le module : latence par cran, et si une rotation rapide s'accumule. Piste si trop lent : ne dessiner que le dernier fond demandé (regrouper les crans) et réduire la qualité JPEG pendant la rotation.
- **Écran de veille : mesures et vidéo à fournir** — mesures à la règle (écran, touche, espaces, distance écran → touches) à mettre dans `screensaver.geometry`, puis une vidéo verticale (piste : image générée par ChatGPT puis animée par un outil IA, en boucle ; vérifier les conditions du service et `ASSETS.md`). À confirmer sur le module : fluidité à 15 i/s, continuité de l'image entre l'écran et les touches, réveil.
- **Pages remplies** : les thèmes sont adoptés dans le vrai `pages.json` (copie de l'ancien : `pages.json.avant-themes`, 19 octobre). Reste à ajouter les applications voulues dans chaque page.
- **Jeux les plus joués** (thème Jeux vidéo) : classement des jeux Steam par temps de jeu. Source à étudier en lecture seule (`~/.steam/steam/userdata/<id>/config/localconfig.vdf`, `appmanifest_*.acf`) ; NeuroHell est un `.desktop` personnel (`neurohell.desktop`).

## 3. À vérifier ou corriger

- **Stabilité du démon** : exécution longue (heures), reprise après veille, débranchement/rebranchement du module (le redessin complet est déjà prévu sur `deckAvailableChanged`), fuites : caches bornés (images, icônes, applications) mais pas mesurés sur la durée.
- **Rechargement à chaud** : confirmé pour un déplacement de touche ; à vérifier : fichier remplacé par un éditeur (écriture en plusieurs étapes), fichier invalide pendant l'exécution (les anciennes pages doivent rester), changement de thèmes pendant qu'un mode est actif (le mode est remis à zéro, voulu).
- **Conflits entre instances** : deux `galleond` sont maintenant exclus (le second quitte). OpenRGB (plugin) et `galleond` écrivent tous deux les anneaux : à ne pas lancer ensemble sans règle (non résolu).
- **Fond de test** : `galleond.conf` était écrasé par les tests (corrigé : les tests utilisent `XDG_CONFIG_HOME` dans `build/`). Vérifier qu'aucun autre test n'écrit dans le vrai `~/.config` ou `~/.cache`.
- Ligne fantôme sous les étiquettes des touches sur les photos : probablement optique, jamais confirmé.
- Vignette vidéo FFmpeg, anti-rebond et lancement Kdenlive/Steam : construits et testés, pas rapportés sur le module.
- Fuzzing sur le matériel réel (le sandbox de libFuzzer bloque ~90 s ici).
- Clavier : RVB par touche **bloqué** (voir `docs/protocol/README.md`). Ne jamais rejouer les écritures du Web Hub (profil en flash) ; les valeurs 3 et 5 de la propriété 3 sont des bootloaders ; la commande 0x0f est une remise à zéro d'usine.

## 4. À développer

1. **Interface graphique de réglage** (demandée : PyQt6) pour configurer touches, molettes, profils, thèmes et fonds d'écran. Décision à prendre avant de coder : PyQt6 (nouvelle dépendance Python, licence GPL compatible) ou QML dans `galleond` (le sélecteur `--apps` existe déjà en QML : `daemon/qml/Apps.qml`, `PagesEditor`, `AppsBackend`). Dans les deux cas : la GUI ne parle au démon que par D-Bus (`org.galleoncore.Deck`) ou par le fichier JSON validé, jamais directement au matériel ; écriture atomique avec sauvegarde `.bak` (déjà fait par `PagesEditor`).
2. **Profils** : plusieurs fichiers de pages et bascule (changement de profil selon l'application active ou au choix) ; le fichier JSON unique actuel est le profil de base.
3. **Sous-menus** (pages « Applications » avec touche Retour : `page` et `back` existent déjà dans le format), Discord, autres raccourcis clavier (l'action `key` existe, liste de touches limitée au pavé numérique : à étendre avec validation) ; opérateurs du pavé (+ − × ÷) : pas de place sur 12 touches, à placer sur une seconde page ou les molettes.
4. **Windows** : backend SMTC derrière `MediaController` (MPRIS est Linux seulement) ; portage hidapi déjà couvert par `Transport`.
5. **Plasmoids KDE** réutilisant `widgets/` (aperçu de l'écran du clavier sur le bureau).
6. **RVB du clavier** : reste côté fork d'OpenRGB, bloqué tant que le protocole d'écriture non persistant n'est pas trouvé.

## 5. Architecture

```
lib/        libgalleon (C++20/Qt6, LGPL-3.0-or-later)
  Protocol      fonctions pures : octets <-> événements/rapports (aucune E/S)
  Transport     interface ; HidTransport (hidapi) = E/S brutes
  StreamDeck    logique : keep-alive 500 ms, lecture, signaux, setKeyImage / setScreenImage / setBrightness / setDialRing
  Layout        géométrie pure
daemon/     galleond (GPL-3.0-or-later), bibliothèque statique galleond_core + main.cpp
  PagesConfig        format JSON, validation stricte, thèmes, actions (media, page, back, app, theme, wallpaper-mode)
  PagesController    touches, molettes, thèmes, modes (thème / fond), anneaux, rechargement à chaud
  MprisController    MediaController sur D-Bus (MPRIS) ; MediaController.h = interface (futur SMTC)
  ScreenController   écran : fond+heure ou piste, ligne du haut, barre du bas, D-Bus org.galleoncore.Deck
  ScreenRenderer     rendu hors écran de widgets/*.qml (Qt Quick, backend logiciel)
  TileRenderer       images des touches ; CoverLoader, VideoThumbnails, DesktopEntries, WallpaperCatalog
  PagesEditor / AppsBackend / qml/Apps.qml   sélecteur d'applications
widgets/    QML pur partagé (WallpaperClock.qml, NowPlaying.qml) : pas de PlasmaCore/Kirigami
openrgb-plugin/   plugin OpenRGB (LampArray en libusb) ; le RVB clavier n'est pas développé ici
tests/ fuzz/ tools/ docs/protocol/ data/devices/ public/wallpaper/ plasmoid/
```

Décisions à ne pas perdre :
- **Toute donnée externe est non fiable** (rapports HID, images, vidéos, JSON) : validation de la longueur, des valeurs, puis de la séquence ; une entrée invalide est ignorée et journalisée. Un fichier de pages avec une seule erreur est refusé en entier.
- **Aucun octet de protocole inventé** : chaque fait de protocole cite sa source (node-elgato-stream-deck, galleon-deck, rust-elgato-streamdeck PR 63, captures de `docs/protocol/`). Le rapport des anneaux `03 24 <led> <r> <g> <b>` (led = (1 − molette) × 4 + segment) vient de la PR 63, confirmé sur le module le 19 octobre 2026.
- **Jamais d'écriture en flash** du clavier ; les écritures matérielles de test sont lancées par l'utilisateur (commandes données une par bloc).
- **Aucune action ne lance un programme sans être dans le fichier de pages** : `gio launch <fichier>` à un seul argument, sans shell ; le fichier doit appartenir à l'utilisateur et ne pas être inscriptible par d'autres.
- **État de longue durée borné** (caches d'images, d'icônes, d'applications, pile de pages) pour que LeakSanitizer ne soit pas le seul garde-fou.
- **Fichiers** : `~/.config/galleoncore/pages.json` (pages, thèmes, molettes, écran), `galleond.conf` (fond retenu), `~/.cache/galleoncore/thumbnails` (vignettes vidéo). Les tests isolent `XDG_CONFIG_HOME`.
- **Modes de molette** : un appui sur une molette configurée `theme` ou `wallpaper-mode` bascule cette molette entre contrôle multimédia et choix (thème / fond) ; sortie par second appui ou après 30 s sans rotation.
- Fonctionnalités gardées volontairement : système de profils et configuration JSON (demande explicite de l'utilisateur).
- Build : `cmake -B build -DGALLEON_WERROR=ON && cmake --build build && ctest --test-dir build` ; aussi `-DGALLEON_SANITIZE=ON`, clang-tidy, `qmllint widgets/*.qml`, `reuse lint` (non installé sur cette machine : à lancer ailleurs avant une publication).

## 6. Prochaines étapes prioritaires

1. Confirmer le service systemd au prochain démarrage de session, et la reprise après veille (le lancement par `systemctl --user` est confirmé).
2. Mesurer la latence du choix du fond ; regrouper les crans si nécessaire.
3. Compléter les pages de thèmes avec les applications voulues.
4. Classement des jeux Steam les plus joués (lecture seule des fichiers Steam).
5. Décider PyQt6 ou QML pour l'interface de réglage, puis la construire par-dessus `PagesEditor` / D-Bus.
6. Fusionner la branche `galleond-wallpaper-clock` après relecture (ne rien pousser sans accord).

## Règle de mise à jour

Après chaque fonctionnalité terminée et testée : la déplacer vers « Implémenté et testé » (avec ses tests et, si confirmé, « sur le module »), retirer ses lignes de « En cours » ou « À développer », et mettre à jour « Prochaines étapes » et la date. Ne pas marquer « sur le module » sans retour de l'utilisateur. Garder ce fichier court : le détail de conception va dans `docs/ROADMAP.md`.
