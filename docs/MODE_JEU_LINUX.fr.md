# Mode Jeu sous Linux : configuration des lanceurs

## Peut-on éviter une option dans chaque jeu ?

Oui dans certains cas : Heroic et Lutris ont une case MangoHud. Pour Steam,
une activation commune au lanceur couvre les jeux Vulkan, notamment de nombreux
jeux Proton. Elle ne couvre pas universellement les jeux OpenGL. Galleon ne sait
pas encore activer MangoHud dans un jeu déjà lancé : les FPS demandent une
capture des images, tandis que NVML et AMD-SMI servent aux capteurs GPU.

Le plus simple est de configurer l'enregistrement une seule fois, puis d'activer
MangoHud dans le lanceur. Il n'est pas nécessaire de recopier une longue variable
`MANGOHUD_CONFIG` dans chaque jeu. Les procédures ci-dessous utilisent ton dossier
`/home/paul/Documents/GalleonFPS` ; adapte ce chemin pour un autre compte.

## Comparaison des cinq lanceurs

| Lanceur | Activation graphique | Portée commune possible | Saisie nécessaire en dernier recours |
| --- | --- | --- | --- |
| Steam | Champ Options de lancement ; pas de case MangoHud commune vérifiée dans Steam natif | Activation du lanceur pour Vulkan ; override persistant pour Steam Flatpak | `mangohud %command%` dans le jeu |
| Heroic | Interrupteur MangoHud dans les paramètres Linux | Valeur globale si proposée par la version ; sinon par jeu | Activer la case, puis utiliser le fichier de journalisation commun |
| Lutris | Options système → Compteur de FPS (MangoHud) | Options système communes ou exceptions par jeu | Anciennes versions : préfixe de commande `mangohud` |
| PlayOnLinux 4 | Configurer → Divers → Commande avant lancement | Par raccourci ; activation héritée du lanceur limitée à Vulkan | `export BEFORE_WINE="mangohud${BEFORE_WINE:+ $BEFORE_WINE}"` |
| Bottles | Bouteille → Préférences → Monitor Performance | Tous les programmes de la bouteille | Aucune commande si l'interrupteur et MangoHud fonctionnent |

« Automatique » signifie ici : après cette activation initiale, chaque lancement
utilise le réglage conservé et démarre l'enregistrement défini dans le fichier
commun. Bosun ne modifie pas lui-même les configurations de ces cinq lanceurs.
Les intitulés et l'emplacement exact des contrôles peuvent varier selon la version.

## 1. Préparer MangoHud une seule fois

Pour Steam ou Lutris natifs sous Ubuntu : installer MangoHud avec le gestionnaire
de logiciels, ou dans un terminal :

```sh
sudo apt install mangohud
mkdir -p "$HOME/.config/MangoHud" "$HOME/Documents/GalleonFPS"
```

Ouvrir `~/.config/MangoHud/MangoHud.conf` dans un éditeur. S'il existe déjà,
conserver ses réglages et ajouter/corriger uniquement ces options, sans doublons :

```ini
autostart_log=1
log_interval=500
output_folder=/home/paul/Documents/GalleonFPS
```

L'enregistrement commence une seconde après l'activation de MangoHud ; les
échantillons sont espacés de 500 ms. L'activation de l'overlay seule ne suffit pas :
il faut aussi cet enregistrement pour fournir les FPS à Galleon.

Une ancienne variable `MANGOHUD_CONFIG` peut prendre le dessus sur ce fichier.
Supprimer seulement les anciennes valeurs ajoutées pour Galleon si elles le
contredisent. Un fichier MangoHud propre à un jeu peut également avoir priorité.

Dans **Bosun → Mode Jeu**, renseigner comme source FPS :

```text
/home/paul/Documents/GalleonFPS
```

Laisser le filtre exécutable vide pour MangoHud, activer Automatique et appliquer.
Galleon sélectionne le CSV récent ; un ancien journal ne simule pas un jeu actif.

## 2. Heroic : activer la case MangoHud

Ouvrir les paramètres d'un jeu, chercher **MangoHud / Afficher les FPS** dans les
réglages Linux et activer l'interrupteur. Selon la version, un réglage global peut
servir de valeur par défaut pour les jeux ; vérifier aussi les réglages du jeu.
Il n'y a ni `%command%` ni commande `mangohud` à mettre dans « Arguments du jeu ».
Avec le fichier commun ci-dessus, le lancement et l'enregistrement suivent la
case du lanceur.

Pour Heroic Flatpak, installer l'extension MangoHud compatible avec son runtime
(voir la section Flatpak). La case ne remplace pas cette installation.

Source : [interrupteur MangoHud de Heroic](https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher/blob/main/src/frontend/screens/Settings/components/Mangohud.tsx).

## 3. Lutris : utiliser les options système

Clic droit sur le jeu → **Configurer → Options système**, puis activer
**Compteur de FPS (MangoHud)**. MangoHud doit être installé pour que cette option
soit disponible. Chercher les options avancées si la version utilisée les masque.

Sur une ancienne version sans cette case, mettre uniquement `mangohud` dans
**Préfixe de commande**. Garder les arguments propres au jeu dans leur champ
habituel. Si des options système communes sont proposées dans les préférences,
on peut y définir MangoHud puis conserver les exceptions par jeu.

Source : [option système MangoHud de Lutris](https://github.com/lutris/lutris/blob/master/lutris/sysoptions.py).

## 4. Steam : méthode simple et fiable par jeu

Bibliothèque → clic droit sur le jeu → **Propriétés → Général → Options de
lancement**. Pour un champ initialement vide, écrire seulement :

```text
mangohud %command%
```

Le fichier commun fournit déjà les réglages de journalisation. Fermer les
propriétés et lancer le jeu. Si le champ contient déjà une commande, l'intégrer
à celle-ci : ne pas ajouter un deuxième `%command%` au hasard.

**`%command%` est une substitution de Steam. Ne pas lancer cette ligne dans un
terminal, Heroic ou Lutris** : ce n'est pas un programme Linux.

## 5. Steam : activation commune pour Vulkan

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

## 6. Flatpak : extension, configuration et accès aux journaux

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

Afin de partager le fichier de configuration et les journaux avec Galleon,
accorder les accès suivants au lanceur concerné. Exemple Heroic :

```sh
flatpak override --user --filesystem="$HOME/.config/MangoHud:ro" --filesystem="$HOME/Documents/GalleonFPS" --env=MANGOHUD_CONFIGFILE="$HOME/.config/MangoHud/MangoHud.conf" com.heroicgameslauncher.hgl
```

Pour Steam Flatpak, remplacer l'identifiant final par `com.valvesoftware.Steam`.
Quitter et relancer le lanceur après un changement. Cette configuration commune
évite d'ajouter la variable dans chacun de ses jeux.

Sources : [installation et activation MangoHud](https://github.com/flightlessmango/MangoHud#flatpak),
[permissions Flatpak](https://docs.flatpak.org/en/latest/sandbox-permissions.html).

## 7. PlayOnLinux 4 : préfixe dans le champ de pré-lancement

Sélectionner le raccourci du jeu, puis **Configurer → Divers / Miscellaneous**.
Il faut sélectionner le programme, pas seulement son disque virtuel. Dans
**Commande à exécuter avant de lancer le programme**, ajouter cette ligne en
conservant les autres commandes éventuelles :

```sh
export BEFORE_WINE="mangohud${BEFORE_WINE:+ $BEFORE_WINE}"
```

Relancer normalement ce raccourci. PlayOnLinux 4 charge ce champ dans le shell du
lancement ; sa fonction Wine applique ensuite `BEFORE_WINE` au Wine déjà choisi.
La partie conditionnelle conserve un éventuel préfixe existant. Cette procédure
ne remplace ni la version de Wine ni le préfixe du jeu.

Ne pas mettre simplement `mangohud` seul dans ce champ : dans le code de POL 4,
ce contenu est un script exécuté avant le raccourci, et non le champ Préfixe de
commande de Lutris. Ne pas y mettre `%command%` non plus.

Pour retirer la configuration, supprimer seulement la ligne ajoutée. Cette
vérification porte sur **PlayOnLinux 4**, pas sur Phoenicis/PlayOnLinux 5.

Sources : [champ de configuration](https://github.com/PlayOnLinux/POL-POM-4/blob/master/python/configurewindow/ConfigureWindowNotebook.py),
[chargement avant le raccourci](https://github.com/PlayOnLinux/POL-POM-4/blob/master/bash/run_app),
[application du préfixe Wine](https://github.com/PlayOnLinux/POL-POM-4/blob/master/lib/wine.lib).

## 8. Bottles : activer la surveillance dans la bouteille

Ouvrir la bouteille du jeu → **Préférences → Affichage / Display**, puis activer
**Monitor Performance / Surveillance des performances**. Bottles utilise MangoHud
pour OpenGL et Vulkan et conserve cette option dans la bouteille : elle peut donc
servir à plusieurs programmes sans modifier leurs arguments.

Le bouton de gestion MangoHud permet notamment de choisir l'affichage au
lancement. Masquer l'affichage n'équivaut pas à fournir les journaux à Galleon :
conserver la configuration d'enregistrement de la section 1. Bottles peut utiliser
`MANGOHUD_CONFIG=read_cfg,no_display` pour charger la configuration tout en
masquant l'overlay.

Pour Bottles Flatpak, utiliser `com.usebottles.bottles` dans les instructions
d'accès au fichier de configuration et au dossier CSV de la section 6. Vérifier
la disponibilité de MangoHud pour ce runtime. Ne pas ajouter un second préfixe
`mangohud` si Bottles l'ajoute déjà. Gamescope suit une autre branche de lancement :
si aucun CSV n'apparaît dans cette configuration, vérifier l'intégration MangoApp
proposée par les versions utilisées.

Sources : [interrupteur de surveillance](https://github.com/bottlesdevs/Bottles/blob/main/bottles/frontend/ui/details-preferences.blp),
[préfixe et variables appliqués par Bottles](https://github.com/bottlesdevs/Bottles/blob/main/bottles/backend/wine/winecommand.py).

## 9. Vérifier puis dépanner

1. Lancer un jeu avec la case ou la méthode choisie.
2. Attendre quelques secondes et ouvrir `Documents/GalleonFPS` : un CSV doit
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
