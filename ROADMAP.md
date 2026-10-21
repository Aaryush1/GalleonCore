# ROADMAP

Référence pour reprendre le développement à tout moment, y compris dans une nouvelle session Claude Code. À mettre à jour après chaque fonctionnalité terminée **et** testée (voir « Règle de mise à jour » en bas). Les notes de conception détaillées (protocole, RGB, OpenRGB, Windows) sont dans [docs/ROADMAP.md](docs/ROADMAP.md) ; les règles de code dans [CLAUDE.md](CLAUDE.md).

État au 21 octobre 2026 : socle Linux/Windows, Setup NSIS, pages initiales à 12 touches et aperçus Bosun fusionnés dans `main` jusqu'à la PR #35. La validation complète de l'arbre fusionné et les CI de `main` ont réussi. Le propriétaire a confirmé le fonctionnement du Setup après mise à jour du firmware Stream Deck depuis l'application officielle Elgato, puis désinstallation de cette application. Le tag [`v0.1.0`](https://github.com/Paullux/GalleonCore/tree/v0.1.0) pointe sur `8634920` et a déclenché la [CI de publication](https://github.com/Paullux/GalleonCore/actions/runs/39674680765) : consulter cette exécution et les dépôts pour leur état effectif. COPR a réussi après retrait d'un dépôt externe invalide. Les premières sources PPA ont compilé et passé CTest, mais Launchpad a refusé les binaires à cause de 46 fichiers datés de 1970 ; une révision Debian `-2` corrige les horodatages sans changer l'archive source publiée. Les essais prolongés et le démarrage à froid sous Windows restent à confirmer. La [PR #31](https://github.com/Paullux/GalleonCore/pull/31), ancien contournement HID expérimental non validé sur le matériel, est fermée sans fusion.

## 1. Implémenté et testé

Le 20 octobre, le propriétaire confirme le fonctionnement du correctif des fonds personnels et du bouton « Parcourir » (PR #24).

« Testé » = tests Qt automatiques dans `tests/`, sous `-Werror`. « Sur le module » = confirmé par l'utilisateur sur le matériel (photo ou retour).

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
| **Galleon Bosun, maquette** (`bosun/`, `galleon-bosun`) : fenêtre QML avec un onglet par thème, la grille des 12 touches dessinée par `TileRenderer` (comme le module), le détail d'une touche (lecture seule), rechargement quand `pages.json` change ; onglets Clavier, Fonds d'écran et Écrans de veille annoncés mais vides. Options `--pages`, `--tab`, `--key`, `--screenshot` | oui (`test_bosun`) | sans objet (fenêtre de bureau : à essayer par l'utilisateur) |
| **Galleon Bosun, onglet Écrans de veille** (tranche 1, lecture seule) : réglages (actif ou non, délai, fps, pendant la lecture, taille de l'image découpée), liste des vidéos avec vignette (FFmpeg, cache partagé avec galleond), taille, fréquence, durée et codec par `ffprobe` (`VideoProbe`, sans shell, délai, protocole `file` seul, sortie bornée), remarques (partie coupée, vidéo plus petite que l'écran, fichier absent, `ffprobe` absent) | oui (`test_videoprobe`, `test_bosun`) | sans objet (fenêtre de bureau : à essayer) |
| **Galleon Bosun, écrans de veille, tranche 2** : réglages modifiables (délai de 1 à 720 min avec raccourcis, images par seconde 1 à 20, pendant la lecture), titre d'une vidéo, retrait d'une vidéo (confirmation par second clic ; le fichier vidéo n'est jamais touché ; la dernière retirée éteint l'écran de veille sans effacer les réglages). Écriture par `PagesEditor` : validée comme tout fichier de pages, atomique, copie `.bak`, fichier déjà invalide laissé intact | oui (`test_pageseditor`, `test_bosun`) | sans objet (fenêtre de bureau : à essayer) |
| **Galleon Bosun, écrans de veille, tranche 3a** : ajouter une vidéo telle quelle. Le premier bouton « Browse… » ouvre le sélecteur natif ; on peut aussi déposer un fichier sur la fenêtre ou écrire son chemin ; il est seulement regardé (vignette, taille, fréquence, durée, codec par `ffprobe`, remarques : partie coupée, plus petit que l'écran, moins d'images par seconde que le réglage) jusqu'à l'ajout ; refus si ce n'est pas une vidéo lisible, si ffprobe ne la lit pas, si la liste est pleine (8) ou si elle y est déjà (même fichier écrit autrement). Le fichier n'est ni copié ni converti. Le texte des remarques est une fonction pure (`VideoAdvice`) | oui (`test_videoadvice`, `test_pageseditor`, `test_bosun`) | sans objet (fenêtre de bureau : à essayer) |
| **Galleon Bosun, écrans de veille, tranche 3b-1 : le moteur du rognage et de la conversion** (sans interface encore). `CropMath` : rectangle de rognage aux proportions de l'image du module, toujours dans la vidéo (centrer, ajuster, déplacer, zoomer autour d'un point). `ScreensaverSplitter::regions` : où tombent l'écran et les 12 touches dans l'image. `VideoConverter` : FFmpeg sans shell, priorité minimale, protocole `file` seul, sans audio, 120 s au plus, un à la fois, écrit sous un nom temporaire puis renomme, jamais par-dessus un fichier, noms sûrs, progression, annulation, délai de 30 min. Résultat dans `~/.local/share/galleoncore/screensavers/` puis ajouté à la liste ; l'original n'est jamais touché. Encodeur libx264, sinon mpeg4. Côté `BosunBackend` : `startConversion`, `cancelConversion`, état `conversion` | oui (`test_cropmath`, `test_videoconverter`, `test_screensaver`, `test_bosun`) | sans objet (pas d'interface encore) |
| **Galleon Bosun, onglets de thème éditables** : une touche fait une application (liste du menu KDE avec icônes), une commande du lecteur (volume et déplacement avec leur pas), l'ouverture d'une page, un retour ou une touche du pavé ; texte et icône réglables ; vider une touche. Le thème (onglet) : nom, couleur (palette ou #rrggbb), ordre, retrait (jamais le dernier ni celui de la page d'accueil ; la page part avec lui sauf si une touche y mène) ; onglet « + » pour en créer un (page vide de 12 touches, nom de page tiré du nom). Les sous-pages (ex. `multimedia-plus`) s'éditent depuis l'onglet dont les touches y mènent (de proche en proche), et seulement celui-là. Chaque onglet peut avoir sa page « plus » (`main-plus`, `jeux-plus`… : nom tiré de celui de la page) : bouton dans l'éditeur de l'onglet, ou case à cocher à la création ; la dernière touche libre y mène, une touche Retour y revient ; elle part avec son onglet quand rien d'autre n'y mène. Tout s'écrit dans `pages.json` (copie `.bak`) que galleond relit seul : le changement atteint le module aussitôt et reste après redémarrage. Barre d'état : galleond tourne-t-il (nom D-Bus), bouton pour le démarrer (`systemctl --user start galleond`) | oui (`test_pageseditor`, `test_pagename`, `test_daemonstatus`, `test_bosun`) | à essayer |
| **Galleon Bosun, écrans de veille : rognage et conversion** (interface). Une vidéo choisie (dépôt ou chemin) s'ajoute telle quelle ou « coupée à la taille du module » : fenêtre de rognage avec le cadre jaune aux proportions de l'image du module (déplacer à la souris, molette ou boutons pour zoomer, centrer) et, dedans, l'écran (cyan) et les 12 touches dessinés là où ils tomberont ; FFmpeg écrit une nouvelle vidéo 720×1258 aux images par seconde du réglage dans `~/.local/share/galleoncore/screensavers/`, barre de progression, bouton Stop ; le résultat est ajouté à la liste ; l'original n'est jamais touché. Une vidéo déjà dans la liste peut être recoupée ; liste pleine ou FFmpeg absent : dit pourquoi. Commande FFmpeg essayée pour de vrai sur la vidéo d'exemple (704×1280 → 720×1258, h264, 15 i/s, 1 s) | oui (`test_bosun`) ; FFmpeg réel essayé à la main | à essayer |
| **Galleon Bosun, onglet Fonds d'écran** : miniatures (par style), clic pour utiliser (appliqué aussitôt si galleond tourne, retenu pour le prochain démarrage dans `galleond.conf`), image en cours repérée (magenta), ajout d'une image personnelle (bouton « Browse… » en premier, dépôt ou chemin ; fenêtre de rognage au format de l'écran 720×384 ; enregistrée en JPEG 1440×768 dans `~/.local/share/galleoncore/wallpaper/mine/`, jamais par-dessus un fichier), suppression de ses propres images seulement (pas celles du projet, pas celle en cours). Côté démon : le catalogue lit plusieurs dossiers (le sien, le système, l'arbre source) au lieu du premier trouvé, se relit à `SetWallpaper` inconnu et à l'entrée du mode choix ; le dossier personnel est conservé même absent au démarrage pour découvrir le premier import sans redémarrer le démon. Fichiers vérifiés (taille, dimensions, contenu) avant décodage | oui (`test_wallpaperlibrary`, `test_wallpapercatalog`, `test_screencontroller`, `test_filenames`) | à essayer |
| **Galleon Bosun, onglet Clavier** : bouton qui ouvre le Web Hub de Corsair (`https://www.corsair.com/web-hub`, page officielle de l'aide Corsair) dans le navigateur de l'utilisateur, avec ce qu'il faut savoir (navigateur Chromium, rien n'est écrit dans la mémoire du clavier par cette application). Pas de webview : lien seul | sans objet (lien) | à essayer |
| **Galleon Bosun, icône dans la zone de notification** : fermer la fenêtre la masque (si la zone existe), clic gauche affiche ou masque, clic droit : Afficher/Masquer, « Quitter et arrêter galleond » (le module repasse en pavé numérique) et « Quitter » (galleond continue), ou « Démarrer galleond » quand il ne tourne pas ; menu refait selon l'état. Sans zone de notification, fenêtre ordinaire. Le contenu du menu est une fonction pure | oui (`test_traymenu`, fermeture = masquage dans `test_bosun`) ; l'icône elle-même ne se teste pas hors bureau | à essayer |
| Plugin OpenRGB (LampArray en libusb) : anneaux des molettes pilotés depuis OpenRGB | oui | oui (les anneaux ; le clavier non, voir §4) |
| **Bureau Windows** : SMTC, Core Audio, SendInput, IPC/verrou par utilisateur, Chrome/App Paths/PWA distinctes, icônes natives, Setup NSIS UTF-8, démarrage de galleond et Bosun minimisé par HKCU Run ; Bosun lance le démon absent | oui (tests natifs, déploiement, installation/mise à niveau/désinstallation avec applications ouvertes et données conservées) | partiel : fonds et pages présents ; stabilité et ouverture de session à revalider |
| **Pages initiales fixes Linux/Windows** : cinq onglets, au plus 12 touches par page, navigateurs/lecteurs et outils système pertinents prioritaires, jeux Steam en premier ; détection unique ; retraits et positions conservés ; `.url` Steam Windows bornés et validés | oui (`test_initialpages`, `test_pagescontroller`, `test_usage`, `test_windowsdesktop`) | nouvelles pages Linux à confirmer ; Windows à revalider après ce correctif |
| **Réactivité Windows** : recherche d’icônes/jaquettes dans un enfant limité, annulation des requêtes périmées, dessin échelonné des 12 touches ; keep-alive connu servi entre rapports JPEG longs | oui (`test_launchercache`, `test_streamdeck`, `test_windowsdesktop`) | retour au logo Corsair et longues sessions à revalider |
| **Réveil sans attente du décodeur** : arrêt asynchrone FFmpeg et nettoyage borné, première action consommée | oui (`test_screensaver`, `test_windowswake`, `test_modes_flow`) | réveil court Linux observé avant ce correctif ; réveil prolongé Linux/Windows à confirmer |

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
4. **Windows : validation matérielle** — les backends et le bureau sont implémentés et testés automatiquement ; la suite porte sur les essais du propriétaire (stabilité, pages à 12 lanceurs, retraits/modifications, réveil prolongé, ouverture de session, lancement réel de jeux/PWA et coexistence RGB). Voir `docs/WINDOWS.md`.
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
- **Validation** : `tools/check.py quick|batch|full|status` (voir CLAUDE.md, « Validation method »). Ciblé pendant le développement, complet (conteneur Ubuntu 24.04 de `tools/ci/`, comme la CI) à la fin d'un lot et avant toute fusion ; le rapport dit `TARGETED` ou `FULL VALIDATION PASSED`, lié à l'arbre exact (`status`).
- Build : `cmake -B build -DGALLEON_WERROR=ON && cmake --build build && ctest --test-dir build` ; aussi `-DGALLEON_SANITIZE=ON`, clang-tidy, `qmllint widgets/*.qml`, `reuse lint` (non installé sur cette machine : à lancer ailleurs avant une publication).

## 6. Prochaines étapes prioritaires

1. (fait) Reprise du module après une veille de l'ordinateur : confirmée. Le réglage NVIDIA de l'utilisateur est un sujet système, hors du projet.
2. (fait) Branche `galleond-wallpaper-clock` fusionnée dans `main` (pull request 5, 19 octobre) ; la CI passe (GCC, Clang + sanitizers, lint).
3. Tester le dernier Setup Windows validé : pages fixes à 12, modifications/retraits conservés, lanceurs Steam/PWA, réveil après attente longue et démarrage de session. Faire le premier essai avec les autres contrôleurs des anneaux arrêtés ; relever toute reprise du logo Corsair. Confirmer séparément la veille prolongée Linux et les cinq pages Linux initiales figées à 12 touches au plus.
4. **Galleon Bosun** : (fait : maquette ; écrans de veille : lecture puis réglages et retrait) sélecteur de fichier natif images/vidéos et découverte du premier fond personnel après démarrage corrigés ; validation sur le module confirmée par le propriétaire ; démarrage de Bosun avec la session (option) ; distribution Linux : recettes PPA (Ubuntu 24.04/26.04), COPR (Fedora 43/44/45/rawhide), AUR et AppImage, CI de construction et publication sur tags dans `tools/packaging/` et `.github/workflows/linux-*.yml` (voir `tools/packaging/README.md` ; PPA et COPR ouverts, secrets requis pour publier, validation matérielle et CI de l’arbre fusionné à confirmer ; badges README/site et documents de contribution, conduite, licences et sécurité ajoutés), puis édition d'une touche (application, libellé, icône, couleur de l'onglet, ajout et retrait), puis onglets fonds d'écran et économiseurs d'écran, puis réinitialisation et lien Web Hub.
5. Mesurer la latence du choix du fond ; regrouper les crans si nécessaire.

## Règle de mise à jour

Avant de la déplacer, les tests ciblés suffisent pour avancer, mais la case « Tests : oui » ne vaut qu'avec une validation complète (`tools/check.py full`) de l'arbre fusionné. Après chaque fonctionnalité terminée et testée : la déplacer vers « Implémenté et testé » (avec ses tests et, si confirmé, « sur le module »), retirer ses lignes de « En cours » ou « À développer », et mettre à jour « Prochaines étapes » et la date. Ne pas marquer « sur le module » sans retour de l'utilisateur. Garder ce fichier court : le détail de conception va dans `docs/ROADMAP.md`.

## 7. État du portage Windows et prochain essai matériel

Le code commun reste dans `main`, avec des backends Linux/Windows séparés.
La [PR #28](https://github.com/Paullux/GalleonCore/pull/28) regroupe les derniers
correctifs ; les [CI et artefacts courants](https://github.com/Paullux/GalleonCore/actions)
font foi pour le commit à essayer. Les détails et limites sont dans
[docs/WINDOWS.md](docs/WINDOWS.md), le packaging dans
[docs/PACKAGING.md](docs/PACKAGING.md).

Le quatrième retour matériel signale un retour au logo Corsair sur les pages
chargées, puis une reprise après environ une minute, sans changement du PID.
Les retraits/modifications pouvaient être annulés par le remplissage automatique.
La tentative de pagination à 22 est donc remplacée par **12 lanceurs initiaux
fixes**, sans découverte sur rechargement/édition. Le même plafond et les cinq
onglets initiaux sont appliqués aux profils Linux neufs ; les fichiers personnels
existants ne sont pas migrés. Les anciens liens Plus générés
sont retirés ; les pages supplémentaires restent dans le fichier pour récupération.
Les raccourcis `.url` Steam sont désormais acceptés avec validation stricte.

Les recherches Shell/Steam sont isolées dans un enfant limité (2,5 s, 64 Kio,
12 requêtes en attente, 64 résultats), le dessin cède la main entre touches et les
transferts longs servent le keep-alive existant. Les tests simulent des recherches
et écritures lentes ; ils ne prouvent pas à eux seuls la résolution matérielle.
La découverte initiale ne charge pas les icônes/jaquettes avant l’ouverture HID.

La capture du propriétaire identifie les anneaux comme `Dynamic Light` dans
OpenRGB officiel : Corsair LampArray VID 1B1C/PID 2B18, MI_03. L’Éclairage dynamique
Windows et OpenRGB peuvent concurrencer galleond ; aucun arbitrage automatique
ni nouvelle commande RGB persistante n’est ajouté. Tester d’abord sans ces autres
écrivains, puis évaluer leur coexistence séparément.

Le diagnostic Linux précédent a montré une déconnexion USB spontanée, puis un
réveil court réussi après réénumération. L’ancien patch expérimental avait été
retiré : ne pas le présenter comme une correction matériellement confirmée.
Le réveil prolongé et le nouveau transfert avec keep-alive restent à vérifier.

Le propriétaire a demandé explicitement le tag `v0.1.0` et le lancement de la
publication après son nouvel essai du Setup. Ce tag a été poussé depuis `main` ;
la réussite de la CI de publication ne vaut pas confirmation des essais Windows
prolongés, ni preuve que les dépôts PPA/COPR/AUR ont effectivement publié les
paquets.

### Retour matériel et suivi HID du 21 octobre

Le propriétaire confirme la pagination manuelle et le comportement attendu une
fois le contrôle acquis, même avec OpenRGB ; le démarrage reste intermittent.
Le suivi `codex/windows-hid-startup` corrige les défauts identifiés dans le cycle
HID : ouverture Windows après initialisation Qt, contrôle du mode non bloquant,
lecture à délai nul, premier keep-alive obligatoire et transitions de reconnexion
visibles avant nouvelle tentative périodique. Des régressions simulées couvrent
ces chemins ; la cause matérielle et le démarrage à froid restent à confirmer.
Les nouveaux liens de pagination Bosun sont `NEXT >` et `< BACK`, sans modifier les
libellés personnels. Voir `docs/WINDOWS.md`. Validation complète exigée avant PR ;
aucune publication avant acceptation matérielle du propriétaire.

### Nouvel essai matériel Windows du 21 octobre

Le changement de fond dans Bosun réveille désormais la veille animée avant
d'afficher l'image choisie. Le test de régression avec faux périphérique valide
la transition veille → fond sous Qt ; l'effet sur le module Windows reste à
confirmer lors du prochain essai matériel.

Le propriétaire confirme que la PR #29 ne résout pas la première prise de
contrôle : images et Bosun fonctionnent, mais les touches et molettes restent
inactives même après dix minutes. Un lancement manuel après démarrage complet
de Windows reproduit le défaut. Le PID de `galleond.exe` reste identique pendant
le retour temporaire au logo Corsair, puis les commandes fonctionnent. Le PID
stable ne permet pas de conclure sur la fermeture du handle HID. Une
instrumentation bornée, sans contenu des rapports ni nouvelle commande HID,
est préparée pour distinguer absence de lecture, rejet du décodage et vraie
reconnexion ; voir `docs/WINDOWS.md`. La cause et la correction matérielles
restent à établir après comparaison des journaux d'une première ouverture en
échec et de la récupération.

Le 21 octobre, le propriétaire a ensuite mis à jour le firmware Stream Deck
depuis l'application officielle Elgato sous Windows, puis désinstallé cette
application. Lors du nouvel essai, touches LCD et molettes ont répondu sans la
panne initiale ; sa capture montre `3.06.005`. Comme les deux changements ont
été faits ensemble, leur effet individuel reste inconnu. Le propriétaire a
ensuite jugé le Setup satisfaisant et demandé le premier tag. Confirmer encore
la stabilité après démarrage à froid et longue veille ; ne pas attribuer la
résolution du défaut HID à un correctif logiciel non vérifié.
