# Feuille de route

Notes de conception du projet (en français). Le README public est en anglais.

## 1. Matériel

- Clavier mécanique full-size : le pavé numérique est remplacé par un module Stream Deck Elgato (12 touches LCD, écran, 2 molettes).
- Switches MLX Pulse, polling 8 kHz, barre d'ambiance RVB, molettes rétroéclairées, double USB-C avec passthrough.
- Deux périphériques USB distincts :
  - partie clavier : `1b1c:2b0c` (d'après une issue OpenDeck) ;
  - partie Stream Deck : `1b1c:2b18` (d'après galleon-deck).
- Résolution de l'écran du haut **à vérifier** : les sources se contredisent (1280×720 ou 720×180). Voir `docs/PROTOCOL.md` de galleon-deck.
- Officiellement, Elgato ne supporte pas la fonction Stream Deck sous Linux.

## 2. Existant communautaire

### Stream Deck : protocole connu ✅
- **galleon-deck** (Python, MIT) : https://github.com/NLMP-DDHS/galleon-deck
  - keep-alive (feature report) toutes les 500 ms pour passer en mode hôte ;
  - images **JPEG** poussées vers les touches et l'écran ;
  - lecture des rapports touches et molettes ;
  - frappe via un clavier virtuel **uinput** ;
  - repli sûr : sans keep-alive, le firmware revient en pavé numérique ;
  - limite KDE Wayland : pas d'accès à la fenêtre active (contournable par un script KWin).
- Le protocole vient de **node-elgato-stream-deck** (MIT), support du Galleon depuis la v7.5.0 : https://github.com/Julusian/node-elgato-stream-deck
- **opendeck-galleon** (Rust, plugin OpenDeck) : https://github.com/shanelord01/opendeck-galleon
- Issues ouvertes : OpenDeck #255 et #272, StreamController #562.

### RVB : non supporté ⚠️
- Protocole Corsair **« V2 » / Bragi**, sur l'interface USB 1 du clavier principal.
- Les requêtes en lecture sont sans danger.
- Une **écriture de couleur avec un mauvais format de buffer a gelé la frappe** jusqu'au débranchement (constat de galleon-deck).
- Aucune trace de support du Galleon dans OpenRGB à ce jour.

## 3. Contrôleur RVB pour OpenRGB

Développé dans un fork d'OpenRGB, pas dans ce dépôt. OpenRGB contient déjà `Controllers/CorsairPeripheralV2Controller/`. Ajouter un appareil revient à :
1. ajouter le PID dans `CorsairPeripheralV2Devices.h/.cpp` ;
2. déclarer le format d'envoi : `CORSAIR_V2_TYPE_SW_COLOUR_BLOCK`, `HW_COLOUR_BLOCK`, `SW_TRIPLETS` ou `HW_TRIPLETS` ;
3. ajouter la disposition (KeyboardLayoutManager, `CORSAIR_V2_KB_LAYOUT_ISO` pour l'AZERTY) ;
4. ajouter les zones annexes : barre d'ambiance, anneaux des molettes ;
5. enregistrer le détecteur.

Exemple de merge request du même type : https://gitlab.com/CalcProgrammer1/OpenRGB/-/merge_requests/2367

Le gel observé par galleon-deck vient très probablement d'un mauvais format de buffer, d'un mauvais nombre de LED ou d'un mauvais adressage.

### Démarche
1. Identifier le PID et l'interface (`lsusb -v`).
2. Interroger le clavier en **lecture seule** d'abord : firmware, série, layout.
3. **Capturer le trafic officiel** avec Corsair Web Hub (WebHID dans Chromium, avec règle udev) pendant que `usbmon` et Wireshark enregistrent.
4. Ajouter l'entrée dans la table des appareils et tester.
5. Mapper les LED une par une.
6. Proposer une merge request au GitLab d'OpenRGB.

### Sécurité
- Un gel n'est **pas une brique** : un débranchement suffit.
- **Ne jamais écrire en flash** (sauvegarde matérielle, mode persistant). Rester en mode logiciel.
- Garder un second clavier branché.

### Estimation
- Format V2 existant : 2 à 4 soirées.
- Variante de protocole : 1 à 3 semaines de soirées.

### Décision : pilote dans le plugin (16 octobre 2026)

Plutôt que d'attendre une version d'OpenRGB qui intègre le contrôleur, le pilote RVB est enregistré **à l'exécution par le plugin**, via l'API des plugins d'OpenRGB (API v5, branche `master` : `RegisterVirtualRGBController`, structure `RGBController_Setup` avec ses rappels `DeviceUpdateLEDs` / `DeviceUpdateMode`). Un OpenRGB standard suffit. Source : `OpenRGBPluginInterface.h` et `RGBControllerInterface.h` d'OpenRGB.

- À prouver d'abord, sans clavier : un plugin minimal qui enregistre un faux contrôleur de 12 LED.
- Les contrôleurs « virtuels » n'ont pas été conçus pour du matériel réel : aucun exemple connu. **Plan B : patcher OpenRGB.**
- Le code Corsair V2 d'OpenRGB n'est pas exposé aux plugins : le format d'envoi est à reprendre dans le plugin (GPL-2.0-or-later), toujours en lecture seule tant que le format n'est pas compris.
- La proposition au cœur d'OpenRGB reste l'objectif à terme.
- Qui écrit sur le clavier (plugin ou `galleond`) reste à trancher.

## 4. Écran du clavier et Plasmoids

Un Plasmoid ne peut pas s'afficher directement sur le Galleon, qui attend des JPEG envoyés en HID.

Approche retenue : **des composants QML purs (dans `widgets/`, sans PlasmaCore), partagés par deux hôtes** :
- `galleond` les rend hors écran (`QQuickRenderControl` ou `grabToImage()`), encode en JPEG et pousse le résultat au Galleon ;
- les mêmes QML sont repris par les Plasmoids de `plasmoid/` ;
- les deux communiquent par **D-Bus**.

Débit : quelques images par seconde. Pas d'animation à 60 fps.

## 5. Architecture et ordre de réalisation

```
libgalleon (C++/Qt)            protocole HID Stream Deck + rendu QML hors écran → JPEG
 ├── galleond + D-Bus           démon autonome
 ├── Plasmoids (KDE)            mêmes widgets QML sur le bureau
 └── plugin OpenRGB (prévu)     configuration, synchro thèmes ↔ RVB
Contrôleur RVB                  → merge request au cœur d'OpenRGB
```

1. libgalleon et galleond sous Linux.
2. Contrôleur RVB, puis merge request à OpenRGB.
3. Plasmoids.
4. Plugin OpenRGB, une fois le contrôleur RVB accepté.
5. Portage Windows.

Les fonctions Stream Deck sortent du périmètre d'OpenRGB (éclairage uniquement) : elles vont dans un plugin séparé, pas dans le cœur.

## 6. Portage Windows

| Brique | Linux | Windows |
|---|---|---|
| Accès HID (hidapi) | ✓ | ✓ (collection « vendor ») |
| Rendu QML → JPEG | ✓ | ✓ |
| Plugin OpenRGB | .so | .dll |
| Frappe des touches | uinput | `SendInput` |
| Plasmoids | ✓ | ✗ (petite fenêtre Qt à la place) |
| IPC | D-Bus | socket local ou QtRemoteObjects |

Conflit possible avec Elgato Stream Deck et Corsair Web Hub, à fermer. Valeur ajoutée faible sous Windows : c'est un bonus.

## 7. Licences

- OpenRGB : GPL-2.0-or-later, combinable avec du GPLv3.
- Le contrôleur RVB versé au cœur d'OpenRGB : GPL-2.0-or-later.
- `lib/` : **LGPL-3.0-or-later**.
- Reste du dépôt : **GPL-3.0-or-later**.
- Dépendances compatibles : galleon-deck et node-elgato-stream-deck (MIT), hidapi, Qt (LGPLv3), frameworks KDE (LGPL).

## 8. Fonds d'écran

Le rendu de l'écran d'état (et éventuellement des touches) se compose en trois couches :

```
3. Tuiles / widgets QML     horloge, capteurs, lecture en cours…
2. Voile de lisibilité      dégradé ou assombrissement sous les tuiles
1. Fond                     image fixe ou courte vidéo, éventuellement étalée sur les 12 touches
```

- Le fond est décodé et mis à l'échelle **une seule fois**, puis mis en cache ; seules les tuiles qui changent sont redessinées.
- Les fonds générés doivent respecter le ratio de la cible et une **zone de sécurité** pour les tuiles.
- Une palette extraite du fond peut colorer les tuiles.
- Éviter les dégradés fins : le clavier reçoit du JPEG, qui les dégrade en blocs.
- Styles envisagés (générés par IA une fois les specs connues) : manga classique, manga cyberpunk, cyberpunk occidental, personnage de jeu vidéo original, portrait de personnage fictif, ambiance cinéma ou série.
- Fonds du projet : [`public/wallpaper/`](../public/wallpaper/), un dossier par style avec un `PROVENANCE.md` (outil, date, prompt de chaque image), sous CC-BY-SA-4.0. Format de travail : 16:9 au plus grand, sujet dans la bande horizontale centrale (doit survivre à un recadrage 4:1), tiers gauche plus calme pour les tuiles.
- Règles légales et de licence : voir [`ASSETS.md`](../ASSETS.md). En bref : les styles sont libres, pas les personnages, marques, personnes réelles ni extraits.

## 9. Géométrie du périphérique

Les dimensions ne sont pas écrites en dur : elles vivent dans [`data/devices/galleon-100-sd.json`](../data/devices/galleon-100-sd.json), lu par `galleond` et par l'appli de réglages. Les valeurs sont vides (`null`) tant qu'elles ne sont pas mesurées.

Le calcul des zones de découpe de la grille de touches est une fonction pure de `libgalleon` (`galleon/Layout.h`, testée dans `tests/test_layout.cpp`) : à partir de la taille des touches et des espaces entre elles, en millimètres, elle découpe le cadre choisi sur l'image en 12 rectangles. Les parties qui tombent dans les espaces sont perdues, ce qui rend l'image continue à l'œil, comme vue à travers une grille.

## 10. Application de réglages

Nom de travail : **Galleon-Discover**. Attention, *Discover* est aussi la logithèque de KDE : le nom est à confirmer avant la première publication. Alternatives : Galleon Studio, Galleon Settings, ou un module des Paramètres système (KCM).

- Vit dans un dossier `settings/` du même dépôt, pas dans une branche séparée.
- **Ne lie pas `libgalleon` directement** : elle parle à `galleond` par D-Bus, car un seul processus peut tenir le clavier (keep-alive).

### Import et recadrage d'images
1. Import par glisser-déposer ou sélecteur de fichiers KDE (PNG, JPEG, WebP), décodage avec une limite mémoire (`QImageReader::setAllocationLimit`), puisque le fichier est une donnée externe.
2. Recadrage par cible (écran d'état, grille des 12 touches, tuile Plasmoid) : cadre verrouillé au ratio de la cible, modes remplir/ajuster/rogner, grille des touches avec leurs espaces, zone de sécurité, indicateur de lisibilité.
3. Bouton « Essayer sur le clavier » : aperçu immédiat via `galleond`, sans enregistrer.
4. Enregistrement **non destructif** dans `~/.local/share/galleoncore/backgrounds/<nom>/` : image originale, `manifest.json` (rectangles de recadrage, origine, licence) et images pré-calculées par cible. Si la géométrie change, on régénère depuis les originaux.

### Fonds animés
- Sélection d'une plage dans une vidéo plus longue : fenêtre de **5 s par défaut**, déplaçable, aux bords extensibles (limites fixées par la mesure du débit).
- Même recadrage que pour les images, boucle fondue en option, pas de son.
- Extraction **une fois pour toutes** à l'enregistrement : recadrage, mise à l'échelle et encodage JPEG de chaque frame à la résolution exacte. `galleond` n'envoie ensuite que des JPEG prêts.
- **FFmpeg appelé en processus séparé** : isole les décodeurs vidéo (surface d'attaque) et évite toute question de licence de liaison.
- Ordre de grandeur : 5 s à 12 images/s = 60 JPEG, quelques Mo par clip.
- Lecture en pause quand la session est verrouillée ou en veille.
- La vidéo est réaliste sur l'écran d'état ; sur les 12 touches (12 JPEG par frame), elle dépendra du débit mesuré.

## 11. Robustesse

### Données externes
Toute donnée externe est non fiable jusqu'à sa validation complète : rapports HID du clavier, images et vidéos importées, fichiers de configuration. On valide dans l'ordre la longueur, les valeurs (identifiant de rapport, index de touche, crans de molette), puis la séquence. Une entrée invalide est rejetée et journalisée, jamais une cause de plantage. Les tests de cas limites (rapports tronqués, mauvais identifiant, index hors bornes) seront écrits à partir des premières captures réelles, pas avant.

### Fuites mémoire
- Les tests tournent sous ASan, UBSan et **LeakSanitizer**, activé explicitement ; le fuzzing détecte aussi les fuites.
- Angle mort : LSan ne voit que la mémoire devenue inaccessible à la sortie. Le vrai risque pour `galleond`, qui tourne des jours, est une **fuite logique** : un cache de frames ou des QObject qui s'accumulent en restant atteignables.
- À prévoir quand le démon affichera réellement des images : un **test d'endurance** qui le fait tourner plusieurs heures (changements de fond, vidéos en boucle, débranchements/rebranchements) en surveillant sa mémoire avec heaptrack ou valgrind/massif. Critère : la mémoire se stabilise au lieu de croître.

## 12. Liste de la réception du clavier

**Avant de brancher**
- [ ] Préparer Wireshark avec `usbmon` (`sudo modprobe usbmon`), garder un second clavier branché

**Identification**
- [ ] Brancher sans rien lancer, observer le mode pavé numérique natif
- [ ] `lsusb -v` et `usbhid-dump` sur `1b1c:2b0c` et `1b1c:2b18`, résultats dans `docs/protocol/`

**Mesures physiques, à la règle** (pour `data/devices/galleon-100-sd.json`)
- [ ] Taille d'une touche LCD et espace entre deux touches, horizontal et vertical
- [ ] Disposition des 12 touches (colonnes × rangées)
- [ ] Taille visible de l'écran d'état

**Stream Deck**
- [ ] Installer galleon-deck pour valider que le module répond sous Linux
- [ ] Lire son `docs/PROTOCOL.md`, confirmer la résolution de l'écran et des touches
- [ ] Mesurer le débit : images par seconde sur l'écran d'état, puis sur les 12 touches

**RVB, prudemment**
- [ ] Ouvrir Corsair Web Hub dans Chromium, changer quelques couleurs, capturer le trafic
- [ ] Lecture seule de notre côté : aucune écriture tant que le format n'est pas compris

**Ensuite, dans GalleonCore**
- [ ] Remplir `data/devices/galleon-100-sd.json` avec les mesures
- [ ] Implémenter le keep-alive et l'envoi d'une première image dans `libgalleon`, avec des tests construits sur les captures

## 9. Prochaines étapes (notes du 18 octobre 2026)

Fait : fond d'écran avec l'heure sur l'écran du Galleon, validé sur le matériel (`galleond --wallpaper …`, `--preview`).

À faire, dans cet ordre :
1. ~~**Touches et molettes**~~ **fait le 19 octobre** (appuis, rotations et images de touches validés sur le matériel) : analyser les rapports d'entrée (`01 00 0c 00` + 12 octets pour les touches, `01 03 03 00 …` pour les molettes, d'après galleon-deck) et émettre les signaux de `StreamDeck`. Puis envoyer des **images de touches** (rapports `02 07`, pas encore essayés sur le module).
2. **Pages de boutons et menus** : chaque touche a une icône et une action ; une touche peut ouvrir une page (menu). Exemples voulus : Discord, lecteur de musique (sous Linux via MPRIS), Ableton (surtout côté Windows), raccourcis clavier via uinput. Les actions et les pages sont décrites dans un fichier de configuration validé comme toute donnée externe.
3. **Écran de veille animé** : fond animé extrait en JPEG une fois pour toutes (voir §8, « Fonds animés »). Piste : une image générée animée avec le modèle `prunaai/p-video`. À vérifier avant : conditions d'utilisation et licence du modèle et du service, aucune œuvre, marque ou personne réelle (`ASSETS.md`), provenance consignée dans un `PROVENANCE.md`.
4. Service systemd utilisateur pour lancer `galleond` à la connexion.

Les icônes affichées par les touches en mode hôte viennent de la mémoire du module, pas de `galleond` : elles seront remplacées par celles des pages de boutons.

### Contrôle de la musique : Linux (MPRIS) et Windows (SMTC)

Source : le dépôt de l'utilisateur, https://github.com/Bit-Scripts/MusicLocal-Discord-Presence (MIT, Python ; `backends/mpris.py` et `backends/smtc.py` lus le 19 octobre 2026). Seuls des faits d'API sont retenus, aucun code n'est copié.

- **Linux, MPRIS** (`org.mpris.MediaPlayer2.*`, objet `/org/mpris/MediaPlayer2`) : état, `Metadata` (`xesam:title`, `xesam:artist` = liste, `mpris:artUrl` = `file://` ou `http(s)://`, `mpris:length` en microsecondes) et `Position` en microsecondes. C'est déjà ce que `MprisController` utilise (état, commandes) ; le projet source choisit simplement le premier lecteur trouvé, alors que `MprisController` préfère celui qui joue.
- **Windows, SMTC** (`GlobalSystemMediaTransportControlsSessionManager`) : la session « courante » du système (`get_current_session`) donne titre, artiste, miniature (flux à lire), `source_app_user_model_id` et la chronologie (position, fin). Les commandes existent dans l'API (lecture/pause, suivant, précédent, arrêt, changement de position) mais **pas le volume** : sous Windows le volume doit passer par l'audio du système (Core Audio), pas par SMTC.
- VLC : le projet source passe par l'API HTTP de VLC. Sous Linux, VLC expose MPRIS : ce détour n'est pas nécessaire.

Conséquence pour l'architecture : l'interface `MediaController` reste la frontière. Une classe `SmtcController` (C++/WinRT) s'y ajoutera pendant la phase Windows ; les pages de boutons n'ont rien à changer. À ajouter ensuite à l'interface : les **métadonnées** (titre, artiste, pochette, position, durée) pour un widget « en cours de lecture » sur l'écran du haut.
