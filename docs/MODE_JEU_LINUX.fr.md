# Mode Jeu sous Linux : guide par lanceur

Faites d’abord la préparation **Commune**, une seule fois. Suivez ensuite
uniquement la section de votre lanceur : **Steam**, **Heroic**, **Lutris**,
**PlayOnLinux** ou **Bottles**. Une fois le réglage enregistré, les prochains
lancements utilisent automatiquement MangoHud et Galleon détecte les mesures.
Les réglages d’un lanceur ne doivent pas être copiés dans un autre.

---

## Commun · une seule fois

1. **Installer MangoHud**, avec le gestionnaire de logiciels de votre distribution.
   Sous Ubuntu, pour une installation native :

   ```sh
   sudo apt install mangohud
   ```

   Pour un lanceur Flatpak, il faut aussi une extension MangoHud adaptée à son
   runtime et les permissions de dossiers : voir **Avancé · Flatpak**, plus bas.

2. Dans **Bosun → Gaming → Linux · Préparer les FPS → Commun**, cliquer sur
   **Configurer MangoHud pour Galleon**. Le bouton :
   - crée le dossier des mesures `~/.local/share/galleoncore/fps` ;
   - crée ou ajuste `~/.config/MangoHud/MangoHud.conf` (ou le dossier XDG configuré) ;
   - conserve les autres options et sauvegarde le fichier existant avant modification ;
   - applique immédiatement ce dossier comme source FPS dans Galleon.

   Les trois options configurées sont les suivantes (exemple pour le compte Paul) :

   ```ini
   autostart_log=1
   log_interval=500
   output_folder=/home/paul/.local/share/galleoncore/fps
   ```

   Le bouton n’installe pas MangoHud et ne modifie pas les lanceurs.

3. Activer **Automatique** dans Gaming. Cliquer sur **Appliquer** si ce réglage
   a changé. Laisser le filtre d’exécutable vide pour MangoHud.
4. Passer à **votre lanceur ci-dessous**, puis relancer le jeu. Un CSV frais doit
   être créé, les FPS doivent apparaître et le mode Jeu doit s’activer. Après la
   fermeture du jeu, Galleon revient à l’accueil après environ dix secondes sans
   nouvelles mesures.

**Si une ancienne configuration existe :** une variable `MANGOHUD_CONFIG` ou un
fichier propre au jeu peut remplacer le fichier commun. Vérifier que son dossier
est le même que celui surveillé par Galleon. Ne pas supprimer les autres réglages
du jeu. Le bouton n’active pas MangoHud dans un jeu déjà lancé.

---

## Steam

1. **Bibliothèque → clic droit sur le jeu → Propriétés → Général → Options de lancement**.
2. Pour un champ initialement vide, coller cette ligne entière :

   ```sh
   MANGOHUD_CONFIG="autostart_log=1,log_interval=500,output_folder=$HOME/.local/share/galleoncore/fps" mangohud %command%
   ```

   Bosun affiche la même ligne avec le chemin absolu de votre compte. Avec la
   préparation commune, `mangohud %command%` seul est également suffisant si
   aucune autre configuration ne remplace les options communes.
3. Fermer les propriétés et lancer le jeu. L’option reste mémorisée.

**Où ne pas la coller :** `%command%` appartient à Steam, pas au terminal ni aux
arguments de Heroic, Lutris, POL ou Bottles. Si le jeu possède déjà des options,
les conserver et intégrer MangoHud sans ajouter un deuxième `%command%`.

**Steam Flatpak :** installer l’extension et donner les accès indiqués dans
**Avancé · Flatpak**. L’activation commune de Steam pour Vulkan est une autre
possibilité, présentée plus bas ; la procédure par jeu reste la plus explicite.

---

## Heroic

Ouvrir les paramètres d'un jeu, chercher **MangoHud / Afficher les FPS** dans les
réglages Linux et activer l'interrupteur. Selon la version, un réglage global peut
servir de valeur par défaut pour les jeux ; vérifier aussi les réglages du jeu.
Il n'y a ni `%command%` ni commande `mangohud` à mettre dans « Arguments du jeu ».
Avec le fichier commun ci-dessus, le lancement et l'enregistrement suivent la
case du lanceur.

Pour Heroic Flatpak, installer l'extension MangoHud compatible avec son runtime
(voir la section Flatpak, plus bas). La case ne remplace pas cette installation.

Source : [interrupteur MangoHud de Heroic](https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher/blob/main/src/frontend/screens/Settings/components/Mangohud.tsx).

### Si des options par jeu sont nécessaires

Après la préparation commune, la case MangoHud suffit normalement. Sinon,
dans **Paramètres du jeu → Avancé → Variables d’environnement**, ajouter :

- **Nom :** `MANGOHUD_CONFIG`
- **Valeur :** `autostart_log=1,log_interval=500,output_folder=/home/paul/.local/share/galleoncore/fps`

Utiliser le chemin absolu de votre compte, sans `$HOME`, `~` ni guillemets dans
ce champ de valeur. Ne rien ajouter à **Arguments du jeu** pour MangoHud.


---

## Lutris

Clic droit sur le jeu → **Configurer → Options système**, puis activer
**Compteur de FPS (MangoHud)**. MangoHud doit être installé pour que cette option
soit disponible. Chercher les options avancées si la version utilisée les masque.

Sur une ancienne version sans cette case, mettre uniquement `mangohud` dans
**Préfixe de commande**. Garder les arguments propres au jeu dans leur champ
habituel. Si des options système communes sont proposées dans les préférences,
on peut y définir MangoHud puis conserver les exceptions par jeu.

Source : [option système MangoHud de Lutris](https://github.com/lutris/lutris/blob/master/lutris/sysoptions.py).

### Si des options par jeu sont nécessaires

Après la préparation commune, la case MangoHud suffit normalement. Sinon,
dans **Configurer le jeu → Options système → Variables d’environnement**, ajouter :

- **Nom :** `MANGOHUD_CONFIG`
- **Valeur :** `autostart_log=1,log_interval=500,output_folder=/home/paul/.local/share/galleoncore/fps`

Utiliser le chemin absolu de votre compte, sans `$HOME`, `~` ni guillemets dans
ce champ de valeur. Ne rien ajouter à **Arguments du jeu** pour MangoHud.


---

## PlayOnLinux 4

Sélectionner le raccourci du jeu, puis **Configurer → Divers / Miscellaneous**.
Il faut sélectionner le programme, pas seulement son disque virtuel. Dans
**Commande à exécuter avant de lancer le programme**, ajouter cette ligne en
conservant les autres commandes éventuelles :

```sh
export MANGOHUD_CONFIG="autostart_log=1,log_interval=500,output_folder=$HOME/.local/share/galleoncore/fps"
export BEFORE_WINE="mangohud${BEFORE_WINE:+ $BEFORE_WINE}"
```

Relancer normalement ce raccourci. PlayOnLinux 4 charge ce champ dans le shell du
lancement ; sa fonction Wine applique ensuite `BEFORE_WINE` au Wine déjà choisi.
La partie conditionnelle conserve un éventuel préfixe existant. Cette procédure
ne remplace ni la version de Wine ni le préfixe du jeu.

Ne pas mettre simplement `mangohud` seul dans ce champ : dans le code de POL 4,
ce contenu est un script exécuté avant le raccourci, et non le champ Préfixe de
commande de Lutris. Ne pas y mettre `%command%` non plus.

Pour retirer la configuration, supprimer seulement les deux lignes ajoutées. Cette
vérification porte sur **PlayOnLinux 4**, pas sur Phoenicis/PlayOnLinux 5.

Sources : [champ de configuration](https://github.com/PlayOnLinux/POL-POM-4/blob/master/python/configurewindow/ConfigureWindowNotebook.py),
[chargement avant le raccourci](https://github.com/PlayOnLinux/POL-POM-4/blob/master/bash/run_app),
[application du préfixe Wine](https://github.com/PlayOnLinux/POL-POM-4/blob/master/lib/wine.lib).

---

## Bottles

Ouvrir la bouteille du jeu → **Préférences → Performances / Performance**
(ou **Affichage / Display** selon la version), puis activer
**Monitor Performance / Surveillance des performances**. Bottles utilise MangoHud
pour OpenGL et Vulkan et conserve cette option dans la bouteille : elle peut donc
servir à plusieurs programmes sans modifier leurs arguments.

Le bouton de gestion MangoHud permet notamment de choisir l'affichage au
lancement. Masquer l'affichage n'équivaut pas à fournir les journaux à Galleon :
conserver la configuration d'enregistrement commune. Bottles peut utiliser
`MANGOHUD_CONFIG=read_cfg,no_display` pour charger la configuration tout en
masquant l'overlay.

Pour Bottles Flatpak, utiliser `com.usebottles.bottles` dans les instructions
d'accès au fichier de configuration et au dossier CSV de la section Flatpak ci-dessous. Vérifier
la disponibilité de MangoHud pour ce runtime. Ne pas ajouter un second préfixe
`mangohud` si Bottles l'ajoute déjà. Gamescope suit une autre branche de lancement :
si aucun CSV n'apparaît dans cette configuration, vérifier l'intégration MangoApp
proposée par les versions utilisées.

Sources : [interrupteur de surveillance](https://github.com/bottlesdevs/Bottles/blob/main/bottles/frontend/ui/details-preferences.blp),
[préfixe et variables appliqués par Bottles](https://github.com/bottlesdevs/Bottles/blob/main/bottles/backend/wine/winecommand.py).

---

## Avancé · Steam : activation commune pour Vulkan

### Steam natif

Pour tester sans modifier les options des jeux, quitter complètement Steam
(**Steam → Quitter**, y compris l'icône de notification), puis lancer :

```sh
env MANGOHUD=1 steam
```

Les jeux Vulkan lancés par cette instance héritent de l'activation. Pour éviter
le terminal ensuite, créer un raccourci d'application utilisateur « Steam avec
FPS Galleon » qui exécute cette même commande, via l'éditeur de menus du bureau.
Il doit remplacer le lancement habituel de Steam pour cette session.

Cette méthode n'ajoute pas d'option à chaque jeu, mais utilise une variable pour
le lanceur. Elle est limitée à Vulkan ; pour un jeu OpenGL qui ne produit pas de
CSV, revenir à la méthode `mangohud %command%` par jeu.

### Steam Flatpak

Après avoir installé l'extension correspondante, on peut activer MangoHud pour
cette application une seule fois :

```sh
flatpak override --user --env=MANGOHUD=1 com.valvesoftware.Steam
```

Quitter puis relancer Steam. C'est un réglage du lanceur, pas une modification de
chaque jeu. Pour retirer uniquement cette activation :

```sh
flatpak override --user --unset-env=MANGOHUD com.valvesoftware.Steam
```

Ces activations globales ne garantissent pas tous les moteurs ou conteneurs.
Elles sont une alternative à tester avant de généraliser la configuration.

---

## Avancé · Flatpak : extension et permissions

L'installation native de MangoHud ne suffit pas nécessairement à un lanceur
Flatpak. Installer l'extension depuis Flathub :

```sh
flatpak install flathub org.freedesktop.Platform.VulkanLayer.MangoHud
```

Choisir la branche correspondant au runtime du lanceur ; les branches ne sont
pas interchangeables. Pour identifier l'installation utilisée :

```sh
flatpak info com.heroicgameslauncher.hgl
flatpak info com.valvesoftware.Steam
```

Dans **Bosun → Gaming → Linux · Préparer les FPS → Flatpak · autoriser les CSV FPS**,
cliquer sur **Actualiser**, puis sur **Autoriser les CSV** pour chaque lanceur
Flatpak installé et utilisé (Heroic, Bottles, Lutris ou Steam). Le bouton ne
modifie que les permissions de lecture du répertoire de configuration MangoHud,
d'écriture dans le dossier des CSV FPS et la variable `MANGOHUD_CONFIGFILE`
pour le lanceur choisi. Il ne donne pas accès au reste du dossier personnel et
n'active pas MangoHud à la place de l'extension du runtime.

Pour effectuer la même opération manuellement dans un terminal, voici l'exemple
pour Heroic (adapter les chemins si XDG_CONFIG_HOME ou XDG_DATA_HOME est personnalisé) :

```sh
flatpak override --user --filesystem="$HOME/.config/MangoHud:ro" --filesystem="$HOME/.local/share/galleoncore/fps" --env=MANGOHUD_CONFIGFILE="$HOME/.config/MangoHud/MangoHud.conf" com.heroicgameslauncher.hgl
```

Pour Steam Flatpak, remplacer l'identifiant final par `com.valvesoftware.Steam`.
Pour Bottles, utiliser `com.usebottles.bottles` et pour Lutris `net.lutris.Lutris`.
Quitter et relancer le lanceur après un changement. Cette configuration commune
évite d'ajouter la variable dans chacun de ses jeux.

Sources : [installation et activation MangoHud](https://github.com/flightlessmango/MangoHud#flatpak),
[permissions Flatpak](https://docs.flatpak.org/en/latest/sandbox-permissions.html).

---

## Vérifier puis dépanner

1. Lancer un jeu avec la case ou la méthode choisie.
2. Attendre quelques secondes et ouvrir `.local/share/galleoncore/fps` : un CSV doit
   apparaître et continuer à être modifié pendant le jeu.
3. Vérifier l'état de la capture dans Bosun et les FPS sur le module.
4. Quitter le jeu : Galleon revient à l'accueil après expiration des données.

Pas de CSV : vérifier l'activation de MangoHud, la configuration réellement lue
et l'accès en écriture au dossier. Overlay présent mais aucun CSV : vérifier
`autostart_log` et `output_folder`. CSV présent mais rien dans Galleon : vérifier
le dossier FPS dans Bosun et cliquer sur Appliquer.

Un jeu OpenGL ancien peut demander les bibliothèques MangoHud de la bonne
architecture. Ne pas installer un paquet 32 bits au nom supposé : sa
disponibilité dépend de la distribution. Gamescope utilise une intégration
MangoApp spécifique ; ne pas cumuler aveuglément les différentes méthodes.

Pour un jeu qui fonctionnait avant l'activation, retirer d'abord uniquement les
réglages MangoHud ajoutés et comparer. Ces instructions ne changent ni le
préfixe Wine ni l'exécutable du jeu.

Références : [configuration officielle MangoHud](https://github.com/flightlessmango/MangoHud/blob/master/data/MangoHud.conf),
[usage, configuration et limites](https://github.com/flightlessmango/MangoHud#normal-usage).

## Compatibilité Vulkan, DXVK et VKD3D-Proton

La collecte Linux se fait sur les images présentées par MangoHud. Galleon lit son
CSV : il n'injecte rien dans DirectX et ne demande pas PresentMon sous Linux.
L'activation et la journalisation décrites plus haut restent nécessaires.

| Jeu / API | Chemin graphique | Collecte attendue | Validation au 24 octobre 2026 |
|---|---|---|---|
| Jeu Linux Vulkan natif | Vulkan → pilote | Oui, couche Vulkan MangoHud | CS2 confirmé par l'utilisateur ; test local `vkcube` et lecture du CSV par Galleon réussis |
| Jeu Windows DirectX 9 | D3D9 → DXVK → Vulkan | Oui, si DXVK et MangoHud sont actifs dans le processus du jeu | Compatibilité architecturale vérifiée ; essai dans un jeu D3D9 restant à faire |
| Jeu Windows DirectX 10 | D3D10 → DXVK → Vulkan | Même méthode | Essai dans un jeu D3D10 restant à faire |
| Jeu Windows DirectX 11 | D3D11 → DXVK → Vulkan | Même méthode | Essai dans un jeu D3D11 restant à faire |
| Jeu Windows DirectX 12 | D3D12 → VKD3D-Proton → Vulkan | Même méthode | Essai dans un jeu D3D12 restant à faire |
| WineD3D / OpenGL | D3D → WineD3D → OpenGL | Possible avec le wrapper OpenGL MangoHud | Hors validation actuelle ; `MANGOHUD=1` seul ne suffit pas |

DXVK implémente Direct3D 9 à 11 sur Vulkan ; VKD3D-Proton implémente Direct3D 12
sur Vulkan. La compatibilité attendue ci-dessus est une déduction de ces chemins
et du support Vulkan de MangoHud, pas une certification de tous les jeux.
Sources : [DXVK](https://github.com/doitsujin/dxvk),
[VKD3D-Proton](https://github.com/HansKristian-Work/vkd3d-proton),
[MangoHud](https://github.com/flightlessmango/MangoHud#normal-usage).

### Test local reproductible du chemin Vulkan natif

Le 24 octobre 2026, Ubuntu 26.04.1, RTX 5060 Ti : lancement de `vkcube` pendant
12 secondes avec `autostart_log=1`, `log_interval=500` et un dossier temporaire.
Le CSV contient des lignes `fps,frametime,...` ; le lecteur réel
`galleond::readFpsLog` retourne le processus `vkcube` et **99,5765 FPS** sur la
dernière ligne. La lecture après arrêt utilise l'heure de modification du fichier
comme heure de référence ; elle vérifie le format et l'extraction. Elle ne remplace
pas un essai de bout en bout sur le module. L'arrêt par `timeout` est volontaire.

```sh
mkdir -p /tmp/galleon-fps-vulkan-test
timeout 12s env MANGOHUD_CONFIG=autostart_log=1,log_interval=500,output_folder=/tmp/galleon-fps-vulkan-test mangohud vkcube --width 320 --height 240
```

Cette commande s'exécute dans un terminal. Elle n'utilise pas `%command%`.
Le test peut afficher une fenêtre. Un statut final 124 de `timeout` indique l'arrêt
prévu au bout de 12 secondes, pas un crash du jeu.

### Limites et incompatibilités à connaître

- **WineD3D :** `PROTON_USE_WINED3D=1` remplace DXVK par une traduction OpenGL.
  L'activation globale Vulkan seule ne couvre alors plus le jeu. Utiliser le
  wrapper `mangohud` du lanceur ; vérifier effectivement la création du CSV.
  Référence : [options Proton](https://github.com/ValveSoftware/Proton#runtime-config-options).
- **32 bits :** un jeu D3D9/D3D10/D3D11 peut être 32 bits même sur Linux 64 bits.
  Wine, le pilote Vulkan et MangoHud doivent proposer l'architecture nécessaire.
  Le succès d'un jeu 64 bits ne valide donc pas un ancien jeu 32 bits. Les paquets
  disponibles dépendent de la distribution et du runtime du lanceur.
- **Pilotes et traduction :** DXVK et VKD3D-Proton ont leurs propres exigences
  Vulkan, qui évoluent selon leur version. Une carte ou un pilote incompatible
  peut empêcher le jeu de démarrer avant toute collecte. Ne pas remplacer les DLL
  d'un jeu ou de Proton uniquement pour activer ce compteur.
- **Flatpak et runtimes Steam :** le MangoHud installé sur l'hôte peut ne pas être
  visible dans le conteneur. Utiliser l'extension adaptée et les permissions du
  dossier CSV, comme indiqué dans « Avancé · Flatpak ».
- **Gamescope :** MangoHud recommande MangoApp (`gamescope --mangoapp`) ; son
  wrapper normal avec Gamescope n'est pas pris en charge. Ne pas considérer cet
  affichage seul comme une validation du CSV attendu par Galleon : vérifier la
  journalisation et la source avant de déclarer la collecte compatible.
- **Anti-triche, protections, autres overlays :** la présence d'une couche
  supplémentaire peut être refusée ou entrer en conflit selon le jeu. Aucun
  contournement n'est prévu. Si le problème apparaît avec MangoHud, désactiver
  uniquement cette couche et comparer ; conserver les protections du jeu.
- **Lanceurs et processus :** une variable appliquée après le démarrage du
  lanceur n'affecte pas ses processus existants. Quitter complètement le lanceur
  puis le relancer. Vérifier que le CSV appartient au jeu, pas à son lanceur.
- **Mesure :** FPS de présentation mesurés par la couche, pas fréquence de
  l'écran, charge GPU ou garantie du nombre d'images réellement affichées après
  composition/génération d'images. Plusieurs jeux ou swapchains demandent une
  vérification du processus et du CSV sélectionné.
- **Capteurs indépendants :** des FPS valides ne garantissent pas les températures
  ou la charge GPU. Dans le test local, MangoHud n'accédait pas à NVML et écrivait
  des valeurs GPU nulles ; cela n'empêchait pas les FPS. Galleon dispose d'un
  fournisseur NVML séparé et conserve ses solutions de repli.

### Validation à terminer dans les jeux Windows

Pour chaque API D3D9, D3D10, D3D11 et D3D12 : choisir un jeu connu fonctionnel,
noter son architecture et les versions du lanceur, de Proton/Wine, de DXVK ou de
VKD3D-Proton. Comparer d'abord sans MangoHud, puis avec l'activation du lanceur
et la journalisation. Vérifier un CSV frais qui évolue, un compteur cohérent avec
l'overlay, le passage de Galleon en mode Jeu puis le retour à l'accueil après
fermeture. Conserver le résultat par jeu ; un test de traduction ne valide pas
les quatre API à lui seul.
