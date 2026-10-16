# GalleonCore
# Projet Galleon 100 SD sous Linux : synthèse

> Contexte : achat envisagé d'un **Corsair Galleon 100 SD** (AZERTY FR, filaire). Objectif : support Linux complet (Kubuntu / KDE Plasma 6) : RVB via OpenRGB, partie Stream Deck (12 touches LCD, écran, 2 molettes), widgets affichés sur l'écran du clavier **et** en Plasmoids sur le bureau. Portage Windows en option.
> Synthèse d'une conversation du 16/10/2026.

---

## 1. Matériel

- Clavier mécanique full-size : le pavé numérique est remplacé par un module Stream Deck Elgato (12 touches LCD, écran, 2 molettes).
- Switches MLX Pulse, polling 8 kHz, barre d'ambiance RVB, molettes rétroéclairées, double USB-C avec passthrough.
- Le clavier se présente comme **deux périphériques USB distincts** :
  - partie clavier : `1b1c:2b0c` (d'après une issue OpenDeck) ;
  - partie Stream Deck : `1b1c:2b18` (d'après galleon-deck).
- Résolution de l'écran du haut **à vérifier** : les sources se contredisent (1280×720 ou 720×180). Voir `docs/PROTOCOL.md` de galleon-deck.
- Officiellement, Elgato ne supporte pas la fonction Stream Deck sous Linux : on dépend du travail communautaire.

## 2. Existant communautaire

### Stream Deck : protocole connu ✅
- **galleon-deck** (Python, MIT) : https://github.com/NLMP-DDHS/galleon-deck
  - keep-alive (feature report) toutes les 500 ms pour passer en mode hôte ;
  - images **JPEG** poussées vers les touches et l'écran ;
  - lecture des rapports touches et molettes ;
  - frappe via un clavier virtuel **uinput** ;
  - repli sûr : sans keep-alive, le firmware revient en pavé numérique ;
  - limite KDE Wayland : pas d'accès à la fenêtre active, donc pas d'auto-switch de profil (contournable par un script KWin).
- Le protocole vient de **node-elgato-stream-deck** (MIT), support du Galleon depuis la v7.5.0 : https://github.com/Julusian/node-elgato-stream-deck
- **opendeck-galleon** (Rust, plugin OpenDeck) : https://github.com/shanelord01/opendeck-galleon
- Issues ouvertes : OpenDeck #255 et #272, StreamController #562.

### RVB : non supporté ⚠️
- Protocole Corsair **« V2 » / Bragi**, sur l'interface USB 1 du clavier principal.
- Les requêtes en lecture sont sans danger.
- Une **écriture de couleur avec un mauvais format de buffer a gelé la frappe** jusqu'au débranchement (constat de galleon-deck).
- Aucune trace de support du Galleon dans OpenRGB à ce jour.

## 3. Driver RVB OpenRGB

OpenRGB contient déjà un contrôleur générique `Controllers/CorsairPeripheralV2Controller/`. Ajouter un appareil revient à :
1. ajouter le PID dans `CorsairPeripheralV2Devices.h/.cpp` ;
2. déclarer le format d'envoi : `CORSAIR_V2_TYPE_SW_COLOUR_BLOCK`, `HW_COLOUR_BLOCK`, `SW_TRIPLETS` ou `HW_TRIPLETS` ;
3. ajouter la disposition (KeyboardLayoutManager, `CORSAIR_V2_KB_LAYOUT_ISO` pour l'AZERTY) ;
4. ajouter les zones annexes : barre d'ambiance, anneaux des molettes ;
5. enregistrer le détecteur.

Exemple de merge request du même type : https://gitlab.com/CalcProgrammer1/OpenRGB/-/merge_requests/2367 (M65 RGB Ultra Wired).

**Le gel observé par galleon-deck vient très probablement d'un mauvais format de buffer, d'un mauvais nombre de LED ou d'un mauvais adressage.**

### Démarche
1. Identifier le PID et l'interface (`lsusb -v`).
2. Interroger le clavier en **lecture seule** d'abord : firmware, série, layout.
3. **Capturer le trafic officiel** avec Corsair Web Hub (WebHID dans Chromium sous Linux, avec règle udev) pendant que `usbmon` et Wireshark enregistrent. On en déduit le format bloc ou triplets, la taille des paquets et le nombre de LED.
4. Ajouter l'entrée dans la table des appareils et tester.
5. Mapper les LED une par une : allumer un index à la fois, noter la touche correspondante. C'est la partie la plus fastidieuse.
6. Proposer une merge request au GitLab d'OpenRGB.

### Sécurité
- Un gel n'est **pas une brique** : un débranchement suffit.
- **Ne jamais écrire en flash** (sauvegarde matérielle, mode persistant) pendant le développement. Rester en mode logiciel.
- Garder un second clavier branché.

### Estimation
- Format V2 existant : **2 à 4 soirées**.
- Variante de protocole : **1 à 3 semaines de soirées**, surtout en analyse de captures.

## 4. Plasmoids et écran du clavier : point d'architecture

Un Plasmoid est du QML rendu par plasmashell sur le compositeur. **Il ne peut pas s'afficher directement sur le Galleon**, qui attend des JPEG envoyés en HID.

Approche retenue : **des composants QML purs (sans PlasmaCore), partagés par deux hôtes** :
- un démon Qt les rend hors écran (`QQuickRenderControl` ou `grabToImage()`), encode en JPEG et pousse le résultat au Galleon ;
- les mêmes QML sont empaquetés en Plasmoid pour le bureau ;
- les deux communiquent par **D-Bus**.

Approches écartées :
- Plasmoid réduit à une télécommande : plus simple, mais ce n'est plus le même widget aux deux endroits.
- Héberger des Plasmoids tiers hors écran : trop dépendant de la containment Plasma.

Débit : quelques images par seconde, suffisant pour une horloge, du monitoring ou la lecture en cours. Pas d'animation à 60 fps.

## 5. Architecture globale

```
libgalleon (C++/Qt)            protocole HID Stream Deck + rendu QML hors écran → JPEG
 ├── plugin OpenRGB (.so/.dll)  onglet de configuration, synchro thèmes ↔ RVB
 ├── démon autonome + D-Bus     pour les utilisateurs sans OpenRGB
 └── Plasmoids (KDE)            mêmes widgets QML sur le bureau
Contrôleur RVB                  → merge request au cœur d'OpenRGB
```

- **RVB** : dans le **cœur** d'OpenRGB (merge request).
- **Stream Deck, écran, widgets** : dans un **plugin séparé**, dans ton propre dépôt. Ces fonctions sortent du périmètre d'OpenRGB, qui se limite à l'éclairage ; une MR au cœur serait probablement refusée. Un plugin peut être référencé dans la liste des plugins installables.
- Le plugin ne dépend que de l'API publique d'OpenRGB, ce qui limite la casse quand l'API de plugins évolue.

### Ordre de réalisation
1. libgalleon et le démon sous Linux : utilisable rapidement.
2. Contrôleur RVB, puis merge request à OpenRGB.
3. Plasmoids.
4. Plugin OpenRGB, une fois le contrôleur RVB accepté.
5. Portage Windows, en dernier.

## 6. Portage Windows

| Brique | Linux | Windows |
|---|---|---|
| Accès HID (hidapi) | ✓ | ✓ (collection « vendor », donc ouvrable) |
| Rendu QML → JPEG | ✓ | ✓ |
| Plugin OpenRGB | .so | .dll, même code |
| Frappe des touches | uinput | `SendInput` à écrire |
| Plasmoids | ✓ | ✗ (petite fenêtre Qt à la place) |
| IPC | D-Bus | socket local ou QtRemoteObjects (multiplateforme) |

Bémols :
- conflit avec Elgato Stream Deck et Corsair Web Hub, qu'il faut fermer ;
- valeur ajoutée faible, puisque le logiciel officiel existe déjà. À traiter comme un bonus.

## 7. Licences

- OpenRGB : **GPL-2.0-or-later**. Le « or later » permet de combiner avec du GPLv3.
- Le contrôleur RVB versé au cœur doit être en **GPL-2.0-or-later**, comme le reste du dépôt.
- Plugin et démon : **GPL-3.0-or-later**, compatible.
- libgalleon : **LGPL-3.0-or-later** recommandée, pour qu'OpenDeck, StreamController et d'autres puissent la réutiliser.
- Dépendances compatibles : galleon-deck et node-elgato-stream-deck en MIT, hidapi (licence au choix), Qt en LGPLv3, frameworks KDE en LGPL.
- La GPLv2 n'est pas « plus permissive » que la v3. La v3 ajoute une clause brevets, l'anti-tivoïsation et la compatibilité Apache 2.0.
- Ce schéma de licences est classique, mais ce n'est pas un avis juridique.

## 8. Difficulté globale

| Brique | Difficulté |
|---|---|
| Pilotage des touches, de l'écran et des molettes | Faible à moyenne |
| Rendu QML hors écran → JPEG | Moyenne |
| Plasmoids et D-Bus | Moyenne |
| RVB (contrôleur OpenRGB) | Élevée, avec du risque |
| Intégration KDE Wayland | Moyenne |

**Verdict : MVP en quelques week-ends.** Le RVB est la seule brique incertaine. Avant d'acheter, pars du principe que le RVB peut rester sur son réglage matériel un moment.

## 9. Prochaines étapes, une fois le clavier reçu

- [ ] `lsusb -v` et `usbhid-dump` sur les deux interfaces
- [ ] Installer galleon-deck pour valider la partie Stream Deck
- [ ] Lire `docs/PROTOCOL.md` et confirmer la résolution de l'écran
- [ ] Tester Web Hub sous Chromium et capturer avec `usbmon` + Wireshark
- [ ] Décoder les trames de couleur
- [ ] Squelette de libgalleon (C++/Qt6, CMake)

## Sources

- galleon-deck : https://github.com/NLMP-DDHS/galleon-deck
- opendeck-galleon : https://github.com/shanelord01/opendeck-galleon
- node-elgato-stream-deck : https://github.com/Julusian/node-elgato-stream-deck
- OpenDeck, issue #255 : https://github.com/nekename/OpenDeck/issues/255
- OpenRGB : https://gitlab.com/CalcProgrammer1/OpenRGB
- Exemple de merge request V2 : https://gitlab.com/CalcProgrammer1/OpenRGB/-/merge_requests/2367
- Fiche Elgato : https://www.elgato.com/us/en/p/galleon-100-sd-stream-deck-integrated-mechanical-keyboard-ch-912a31i-na

