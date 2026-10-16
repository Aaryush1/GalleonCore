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

## 8. Prochaines étapes, à la réception du clavier

- [ ] `lsusb -v` et `usbhid-dump` sur les deux interfaces, résultats dans `docs/protocol/`
- [ ] Installer galleon-deck pour valider la partie Stream Deck
- [ ] Lire `docs/PROTOCOL.md` de galleon-deck et confirmer la résolution de l'écran
- [ ] Implémenter le keep-alive et l'envoi d'images dans `libgalleon`
- [ ] Tester Web Hub sous Chromium et capturer avec `usbmon` + Wireshark
- [ ] Décoder les trames de couleur
