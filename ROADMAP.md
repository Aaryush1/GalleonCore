# ROADMAP

Référence pour reprendre le développement à tout moment, y compris dans une nouvelle session Claude Code. À mettre à jour après chaque fonctionnalité terminée **et** testée (voir « Règle de mise à jour » en bas). Les notes de conception détaillées (protocole, RGB, OpenRGB, Windows) sont dans [docs/ROADMAP.md](docs/ROADMAP.md) ; les règles de code dans [CLAUDE.md](CLAUDE.md).

État au 19 octobre 2026, branche `main` (pull request 5 fusionnée, CI verte).

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
| Thèmes : pages colorées (Contrôle jaune, Multimédia cyan, Son rouge, Jeux bleu marine, Système vert, Pavé numérique blanc) ; clic molette gauche = mode choix du thème, rotation = thème suivant/précédent, second clic ou 30 s = retour au contrôle | oui | oui |
| Ligne de couleur et nom du thème en haut de l'écran ; bandeau `◀ Nom ▶` pendant le choix | oui | oui |
| Anneaux des molettes indépendants : gauche = couleur du thème, droite = blanc (contrôle) ou magenta (choix du fond) ; rapport `03 24` par LED | oui | oui (couleurs vues) |
| Choix du fond par la molette droite : clic = mode fond (anneau et barre du bas magenta), rotation = fond suivant/précédent, second clic ou 30 s = retour ; le fond s'affiche même si une piste joue | oui | oui (validé par l'utilisateur) |
| Un seul `galleond` à la fois : le nom D-Bus est pris avant d'ouvrir le module, un second démarrage s'arrête (code 1) sans toucher au module | manuel | à confirmer |
| Service systemd utilisateur (`tools/systemd/galleond.service`, variante `galleond-dev.service` pour l'arbre de compilation) | `systemd-analyze verify` | démarre et pilote le module ; **démarrage à l'ouverture de session confirmé après un redémarrage de la machine** (19 octobre, 14:45 : module ouvert, pages chargées, fond affiché sans commande) ; **reprise après veille de l'ordinateur confirmée côté module** (19 octobre : même processus avant/après, le module a continué à répondre et la veille animée a démarré ; le message « main thread busy 107180 ms » est le temps gelé pendant la veille). Le problème de réveil de la carte graphique (NVIDIA RTX 5060 Ti, `PreserveVideoMemoryAllocations: 0`) est hors du projet |
| Écran de veille animé : vidéo verticale (FFmpeg → images brutes) découpée pour l'écran 720×384 et les 12 touches 160×160, boucle, délai d'inactivité, réveil par touche/molette/début de piste, suspension des autres contrôleurs ; section `screensaver` du JSON (géométrie en mm exigée) | oui (découpage, FFmpeg réel, réveil, médias) | oui : essai du 19 octobre avec les deux vidéos et une géométrie *estimée* (« un vrai succès » selon l'utilisateur) ; géométrie désormais **mesurée par photo** (±0,5 mm, voir `data/devices/galleon-100-sd.json`) ; l'utilisateur a refait les images avec l'action en haut ; la veille est dans son vrai `pages.json` (5 min, 2 vidéos de `public/Screensavers/`, sans logo, avec `PROVENANCE.md` complet (prompts de ChatGPT Images et de `p-video`) et licence CC-BY-SA-4.0) |
| Débit du module mesuré (`--measure-throughput`) : écran 44,7 i/s, touches 35 tours/s, écran + touches 20 tours/s | — | oui (mesuré le 19 octobre) |
| Thème « Pavé numérique » (touches = clavier virtuel uinput, liste fixe de touches, touche maintenue tant qu'on appuie) choisi tout seul quand aucun média ne joue, Contrôle quand un lecteur joue (`"when"`) ; touches teintées d'une version sombre de la couleur du thème | oui | oui (confirmé par l'utilisateur le 19 octobre) (retour de l'utilisateur) |
| Test de bout en bout des molettes (`test_modes_flow` : vrais widgets, faux module, même câblage que `galleond` via `connectPagesToScreen`) : thème sans média, rotation, volume rendu à la sortie du mode, choix du fond validé | oui | — |
| Molette gauche sans lecteur : règle le volume du système (`wpctl`, PipeWire) ; le fond choisi reste 4 s à l'écran avant que la lecture reprenne ; la veille laisse au moins la moitié du temps du démon libre | oui | oui (confirmé par l'utilisateur le 19 octobre) |
| Clic de molette : le cran parasite juste avant ou après l'appui est ignoré (fenêtre de 350 ms avant l'appui, 250 ms après l'appui ou le relâchement), pour que le fond ou le thème choisi ne bouge pas à la validation | oui (`test_modes_flow`) | oui (confirmé par l'utilisateur le 19 octobre) |
| Choix du fond validé tout seul après 4 s sans toucher la molette (`screen.wallpaper_validate_seconds`) ; le fond choisi reste affiché avec un décompte (`wallpaper_hold_seconds`) avant le retour de la lecture | oui | oui (confirmé par l'utilisateur le 19 octobre) |
| Choix du fond précis : la molette compte chaque cran, le premier cran est dessiné tout de suite (≈45 ms mesurées : rendu 20 ms, encodage 1 ms, envoi ≈ 25 ms) et la rafale suivante est regroupée, le réglage n'est écrit qu'à la validation, ordre naturel stable du catalogue (styles par nom, `-2` avant `-10`), nom et position (`portrait-01   10/24`) affichés pendant le choix | oui | oui (confirmé par l'utilisateur le 19 octobre) |
| Lecture des molettes : tous les rapports en attente sont lus à chaque tour (avant : un seul toutes les 10 ms, une file pouvait retarder un cran de plusieurs secondes) ; témoins de blocage du démon dans le journal | oui (`test_streamdeck`) | oui (confirmé par l'utilisateur le 19 octobre) (retard d'environ 2 s mesuré sur la vidéo du 19 octobre) |
| Choix du fond : après un cran accepté, les rotations sont ignorées (pas mises en file) jusqu'à ce que l'écran ait réellement affiché le fond du cran, puis pendant un repos `screen.wallpaper_step_delay_ms` (150 par défaut, 0 = aucune règle) ; délai de secours 1,5 s ; images allégées pendant le choix | oui (`test_modes_flow`, `test_pagescontroller`) | oui (confirmé par l'utilisateur le 19 octobre) |
| Choix du fond : au clic, les rotations sont ignorées jusqu'à l'affichage du nom et de la place (`NN/24`) ; le décompte de validation (`wallpaper_validate_seconds`, 4 s) s'affiche pendant que le choix reste possible (anneau magenta), l'anneau repasse en blanc à la fin ; puis le fond reste `wallpaper_hold_seconds` secondes avant la lecture | oui | oui (confirmé par l'utilisateur le 19 octobre) |
| Affichage immédiat : le module n'affiche une image que si un second envoi la suit ; chaque image (écran et touches) est envoyée deux fois de suite, sauf les images de la vidéo de veille (`flush = false`) | oui | écran : oui (vidéo du 19 octobre, variante B) ; touches : oui (confirmé par l'utilisateur le 19 octobre) |
| Applications et jeux les plus utilisés : `sort: usage` (score décroissant 30 jours par lancement, fichier borné), étoile sur le n°1 ; `steam: {max}` remplit les touches libres avec les jeux Steam installés (temps de jeu et dernier lancement lus localement, sans réseau ni clé) ; action `steam` ; option `menu: {category, max}` : applications du menu KDE d'une catégorie (`Game`), classées par usage et, pour un raccourci Steam, par le temps de jeu local de son jeu | oui (`test_usage`, `test_pageorder`, `test_desktopentries`, `test_pagescontroller`, `test_pagesconfig`) | à confirmer |
| Commandes média en plus : `shuffle`, `repeat`, `mute` (son du système par `wpctl`), `raise`, `fullscreen`, `restart` (MPRIS) ; les touches aléatoire et répéter affichent leur état (mots réglables par `state_labels`, fond plus clair quand actif ; MPRIS ne distingue pas album et liste) | oui (`test_mprisController`, `test_pagescontroller`, `test_pagesconfig`) | à confirmer |
| Plugin OpenRGB (LampArray en libusb) : anneaux des molettes pilotés depuis OpenRGB | oui | oui (les anneaux ; le clavier non, voir §4) |

## 2. En cours

- ~~Aperçu fluide du choix du fond~~ — réglé le 19 octobre (cause : le module n'affiche une image que si un second envoi la suit ; voir `docs/protocol/README.md`). Ancien texte : chaque cran renvoie un JPEG de l'écran. À mesurer sur le module : latence par cran, et si une rotation rapide s'accumule. Piste si trop lent : ne dessiner que le dernier fond demandé (regrouper les crans) et réduire la qualité JPEG pendant la rotation.
- **Écran de veille : composition** — avec la vraie géométrie (écran 62,6 × 33 mm, touches 13,7 mm, pas 19,1 mm, 5,4 mm entre écran et touches) l'écran n'occupe que **30 % de la hauteur** de l'image : le visage doit être dans les 25 % du haut. Ancien texte : deux vidéos reçues (704×1280, 24 i/s, 9 s) : « Hacker dans la métropole néon » (jaune, réaliste) et « Nuit gaming sous la lune rose » (rose, animée), listées dans `~/.config/galleoncore/pages-veille-test.json` avec une géométrie *estimée* (pas mesurée) ; à ne pas ajouter au dépôt sans vérifier `ASSETS.md` (marques visibles dans l'image, conditions du service de génération). Mesures à la règle (écran, touche, espaces, distance écran → touches) à mettre dans `screensaver.geometry`, puis une vidéo verticale (piste : image générée par ChatGPT puis animée par un outil IA, en boucle ; vérifier les conditions du service et `ASSETS.md`). À confirmer sur le module : fluidité à 15 i/s, continuité de l'image entre l'écran et les touches, réveil.
- **Pages remplies** : fait le 19 octobre (Multimédia et Plus, Création Musicale, Jeux vidéo, Système, Pavé numérique). Le contenu est dans le `pages.json` de l'utilisateur ; reste à le confirmer sur le module.

## 3. À vérifier ou corriger

- **Stabilité du démon** : exécution longue (heures), reprise après veille, débranchement/rebranchement du module (le redessin complet est déjà prévu sur `deckAvailableChanged`), fuites : caches bornés (images, icônes, applications) mais pas mesurés sur la durée.
- **Rechargement à chaud** : confirmé pour un déplacement de touche ; à vérifier : fichier remplacé par un éditeur (écriture en plusieurs étapes), fichier invalide pendant l'exécution (les anciennes pages doivent rester), changement de thèmes pendant qu'un mode est actif (le mode est remis à zéro, voulu).
- **Conflits entre instances** : deux `galleond` sont maintenant exclus (le second quitte). OpenRGB (plugin) et `galleond` écrivent tous deux les anneaux : à ne pas lancer ensemble sans règle (non résolu).
- **Isolation des tests** : les tests écrasaient `galleond.conf` (corrigé : `XDG_CONFIG_HOME` isolé, dans le code des tests et par ctest) et leurs faux lecteurs MPRIS apparaissaient sur le bus de l'utilisateur, ce qui perturbait un `galleond` en marche (corrigé : ctest lance chaque test sous `dbus-run-session`). Ne lancer les tests que par `ctest`. Vérifier qu'aucun autre test n'écrit dans le vrai `~/.config` ou `~/.cache`.
- Ligne fantôme sous les étiquettes des touches sur les photos : probablement optique, jamais confirmé.
- Vignette vidéo FFmpeg, anti-rebond et lancement Kdenlive/Steam : construits et testés, pas rapportés sur le module.
- Fuzzing sur le matériel réel (le sandbox de libFuzzer bloque ~90 s ici).
- Clavier : RVB par touche **bloqué** (voir `docs/protocol/README.md`). Ne jamais rejouer les écritures du Web Hub (profil en flash) ; les valeurs 3 et 5 de la propriété 3 sont des bootloaders ; la commande 0x0f est une remise à zéro d'usine.

## 4. À développer

1. **Galleon Bosun** (interface graphique de réglage ; nom choisi le 19 octobre). Décision : **QML** (Kirigami) + un petit module C++ qui réutilise `parsePagesConfig`, `PagesEditor` et le rendu des touches (pas de seconde validation à maintenir) ; application séparée `galleon-bosun`. Elle ne parle au démon que par D-Bus ou par le fichier JSON validé, jamais au matériel ; écriture atomique avec sauvegarde datée avant chaque enregistrement. Onglets prévus (à valider avec l'utilisateur au fur et à mesure) :
   - **Clavier / LED** : lien vers le Corsair Web Hub, ouvert par `xdg-open` (navigateur ou PWA de l'utilisateur). Pas de webview intégrée tant que les conditions d'utilisation de Corsair n'ont pas été lues et jugées compatibles ; explication honnête de ce qui marche sous Linux (OpenRGB).
   - **Un onglet par thème** : touches par défaut mais modifiables (ajout, retrait, icône), couleur de l'onglet, ajout et suppression d'onglet (garde-fous : jamais le dernier thème ni la page de repli ; une cible de `Retour` / `Plus` ne peut pas disparaître).
   - **Fonds d'écran** : miniatures avec nom, ajout d'image personnalisée, rognage au format de l'écran (720×384). Les images de l'utilisateur restent dans son dossier, hors dépôt.
   - **Économiseurs d'écran** : choix, ajout de vidéo avec résolution et fps affichés (plafond mesuré : environ 20 i/s écran et touches ensemble), rognage graphique sur la vraie géométrie, conversion FFmpeg en arrière-plan (arguments fixes, pas de shell, entrée traitée comme non fiable).
   - **Réinitialiser** : par onglet et global, avec confirmation et sauvegarde préalable.
   À trancher : aperçu en direct sur le module pendant l'édition (demande une commande de plus au démon).
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
- **CI** (GitHub Actions, Ubuntu 24.04) : Qt **6.4**, plus ancien que celui du poste de développement (6.10) ; GCC, Clang + ASan/UBSan + fuzz, lint (clang-tidy 18, qmllint, reuse). Pièges rencontrés : `Locale.toString` n'existe pas en 6.4 (le widget utilise `Date.toLocaleTimeString`), un `QLocale` C++ n'y est pas enveloppé en `Locale` (la langue est passée par son nom), `QTimeZone::UTC` demande Qt 6.5, Qt Quick 6.4 laisse un cycle d'images à la sortie (suppression ciblée dans `tests/lsan.supp`). Pour reproduire : un conteneur `ubuntu:24.04` avec `qt6-base-dev qt6-declarative-dev` et les modules `qml6-module-*`. `main` est protégée : tout passe par une pull request aux trois vérifications vertes.
- Build : `cmake -B build -DGALLEON_WERROR=ON && cmake --build build && ctest --test-dir build` ; aussi `-DGALLEON_SANITIZE=ON`, clang-tidy, `qmllint widgets/*.qml`, `reuse lint` (non installé sur cette machine : à lancer ailleurs avant une publication).

## 6. Prochaines étapes prioritaires

1. (fait) Reprise du module après une veille de l'ordinateur : confirmée. Le réglage NVIDIA de l'utilisateur est un sujet système, hors du projet.
2. (fait) Branche `galleond-wallpaper-clock` fusionnée dans `main` (pull request 5, 19 octobre) ; la CI passe (GCC, Clang + sanitizers, lint).
3. Confirmer sur le module les pages remplies, le classement par usage, les nouvelles touches média et le lancement de BIT SCRIPTS Musique.
4. **Galleon Bosun** : maquette QML (onglets, grille des 12 touches rendue par le moteur existant, détail d'une touche), puis onglets fonds d'écran et économiseurs d'écran, puis réinitialisation et lien Web Hub.
5. Mesurer la latence du choix du fond ; regrouper les crans si nécessaire.

## Règle de mise à jour

Après chaque fonctionnalité terminée et testée : la déplacer vers « Implémenté et testé » (avec ses tests et, si confirmé, « sur le module »), retirer ses lignes de « En cours » ou « À développer », et mettre à jour « Prochaines étapes » et la date. Ne pas marquer « sur le module » sans retour de l'utilisateur. Garder ce fichier court : le détail de conception va dans `docs/ROADMAP.md`.
