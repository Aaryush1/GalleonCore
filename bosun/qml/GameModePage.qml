// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
Flickable {
    id: page
    objectName: "gameModePage"
    required property var widgets
    property bool advanced: false
    property var flatpakLaunchers: []
    property bool flatpakError: false
    Component.onCompleted: {
        if (!widgets.windowsCapture) flatpakLaunchers = widgets.flatpakGameLaunchers()
    }
    // The confirmed capture state also covers snapshots without the process flag.
    readonly property bool captureRunning: !!widgets.captureStatus.running || widgets.captureStatus.state === "capturing"
    readonly property bool captureReady: !!widgets.captureStatus.installed && widgets.captureStatus.state === "waiting" && !captureRunning && !widgets.captureStatus.starting
    contentWidth: width
    contentHeight: content.height
    clip: true
    component SetupText: Text {
        width: parent.width
        wrapMode: Text.Wrap
        color: "#b8bec6"
        font.pixelSize: 14
    }
    component SetupCode: TextEdit {
        width: parent.width
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.Wrap
        color: "#80e5d1"
        font.pixelSize: 14
    }
    component SetupTitle: Text {
        color: "white"
        font.pixelSize: 18
        font.bold: true
    }
    component SetupDivider: Rectangle {
        width: parent.width
        height: 1
        color: "#44ffffff"
    }
    function applyEdits() {
        // Keep pending text before backend notifications refresh the bound fields.
        const source = fpsSource.input.text, process = fpsProcess.input.text
        const program = presentMon.input.text
        const key = gameProcess.input.text.trim().toLowerCase()
        const title = gameTitle.input.text, image = gameImage.input.text
        if (source !== (widgets.fpsConfig.source || "") || process !== (widgets.fpsConfig.process || "")) {
            if (!widgets.setFpsSource(source, process)) return
        }
        if (widgets.windowsCapture && program !== (widgets.gameConfig.presentMon || "")) {
            if (!widgets.setCaptureOptions(widgets.gameConfig.managedCapture !== false, program)) return
        }
        if (key !== "") {
            const existing = (widgets.gameConfig.profiles || {})[key] || {}
            if (title !== (existing.title || "") || image !== (existing.image || "")) {
                if (!widgets.setGameProfile(key, title, image)) return
            }
        }
        widgets.apply()
    }
    Column {
        id: content
        width: page.width
        spacing: 14
        Text { text: qsTr("Mode Jeu · 720 × 384"); color: "white"; font.pixelSize: 20; font.bold: true }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#c9ced6"
            text: page.widgets.windowsCapture
                ? qsTr("Start your Steam game: Galleon detects it and starts FPS capture automatically. Install the official PresentMon MSI once, with its shared service. Click Apply to save changes.")
                : qsTr("Automatic game artwork, FPS and CPU/GPU gauges. The keyboard screensaver stays off during an active capture. Click Apply to save changes.")
        }
        Row {
            spacing: 12
            Pill { objectName: "applyGameMode"; text: qsTr("Apply"); selected: page.widgets.pending; onClicked: page.applyEdits() }
            Pill {
                text: page.widgets.gameConfig.automatic !== false ? qsTr("Automatic: on") : qsTr("Automatic: off")
                onClicked: page.widgets.setGameOptions(page.widgets.gameConfig.automatic === false, !!page.widgets.gameConfig.localSensors)
            }
            Pill {
                text: page.widgets.gameConfig.localSensors ? qsTr("Local sensors: on") : qsTr("Local sensors: off")
                onClicked: page.widgets.setGameOptions(page.widgets.gameConfig.automatic !== false, !page.widgets.gameConfig.localSensors)
            }
            Text {
                height: 32; verticalAlignment: Text.AlignVCenter
                text: page.widgets.pending ? qsTr("Changes not applied") : qsTr("Settings applied")
                color: page.widgets.pending ? "#ffb74d" : "#9be7a0"
            }
        }
        Row {
            visible: page.widgets.windowsCapture
            spacing: 12
            Pill {
                objectName: "startPresentMon"
                text: page.captureRunning ? qsTr("Arrêter la capture FPS")
                    : page.widgets.captureStatus.starting ? qsTr("Démarrage de la capture FPS…")
                    : page.captureReady ? qsTr("Prêt — lancez un jeu")
                    : page.widgets.captureStatus.installed ? qsTr("Démarrer la capture FPS") : qsTr("Installer PresentMon")
                selected: page.captureRunning
                enabledLook: !page.captureReady && !page.widgets.captureStatus.starting && page.widgets.captureStatus.state !== "offline"
                onClicked: {
                    if (page.captureRunning) page.widgets.requestGameCapture(false)
                    else if (!page.widgets.captureStatus.installed) Qt.openUrlExternally("https://github.com/GameTechDev/PresentMon/releases/latest")
                    else page.widgets.requestGameCapture(true)
                }
            }
            Pill { text: page.advanced ? qsTr("Hide advanced settings") : qsTr("Advanced settings"); onClicked: page.advanced = !page.advanced }
        }
        Text {
            objectName: "gameCaptureStatus"
            visible: page.widgets.windowsCapture
            width: parent.width; wrapMode: Text.Wrap; color: "#aebac6"
            text: page.widgets.captureStatus.message || qsTr("Waiting for the daemon…")
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#aebac6"
            text: page.widgets.gameState.active ? qsTr("Capture active: ") + page.widgets.gameState.title + " (" + page.widgets.gameState.process + ")"
                : qsTr("Waiting for a live FPS capture. Returns home after 10 seconds without fresh data.")
        }
        Rectangle {
            width: Math.min(720, page.width)
            height: width * 384 / 720
            color: "#101a25"; border.color: "#4b6574"
            Loader {
                anchors.fill: parent
                source: "qrc:/homewidgets/GameScreen.qml"
                onLoaded: item.stateData = Qt.binding(() => page.widgets.gameState)
            }
        }
        Row {
            visible: page.widgets.windowsCapture && page.advanced
            spacing: 12
            Pill {
                text: page.widgets.gameConfig.managedCapture !== false ? qsTr("Capture: automatic") : qsTr("Capture: manual CSV")
                onClicked: page.widgets.setCaptureOptions(page.widgets.gameConfig.managedCapture === false, presentMon.input.text)
            }
        }
        Field {
            id: presentMon; objectName: "presentMonPath"
            visible: false
            width: Math.min(720, page.width); maximumLength: 4096
            value: page.widgets.gameConfig.presentMon || ""
            placeholder: qsTr("PresentMon console path (optional; MSI detected automatically)")
            onEdited: text => page.widgets.setCaptureOptions(page.widgets.gameConfig.managedCapture !== false, text)
        }
        Text {
            visible: page.widgets.windowsCapture && page.advanced
            width: parent.width; wrapMode: Text.Wrap; color: "#aebac6"
            text: (page.widgets.captureStatus.program || "") + "\n" + (page.widgets.captureStatus.detail || "")
        }
        Text { visible: !page.widgets.windowsCapture || (page.advanced && page.widgets.gameConfig.managedCapture === false); text: qsTr("FPS capture source"); color: "white"; font.pixelSize: 18; font.bold: true }
        Row {
            visible: !page.widgets.windowsCapture || (page.advanced && page.widgets.gameConfig.managedCapture === false)
            spacing: 12
            Field {
                id: fpsSource; objectName: "gameFpsSource"
                width: Math.min(560, page.width * 0.5); maximumLength: 4096
                value: page.widgets.fpsConfig.source || ""
                placeholder: qsTr("FPS CSV file or log folder (absolute path)")
                onEdited: text => page.widgets.setFpsSource(text, fpsProcess.input.text)
            }
            Field {
                id: fpsProcess; maximumLength: 260
                value: page.widgets.fpsConfig.process || ""
                placeholder: qsTr("Game executable (optional)")
                onEdited: text => page.widgets.setFpsSource(fpsSource.input.text, text)
            }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#aebac6"
            visible: !page.widgets.windowsCapture || page.advanced
            text: qsTr("MangoHud CSV on Linux, PresentMon --v1_metrics CSV on Windows. The source is shared with the home FPS widget. Local sensors optionally read LibreHardwareMonitor at 127.0.0.1:8085; missing measurements show a dash.")
        }
        Column {
            objectName: "linuxLauncherHelp"
            visible: !page.widgets.windowsCapture
            width: parent.width
            spacing: 14
            SetupTitle { text: qsTr("Linux · Préparer les FPS") }
            SetupText { text: qsTr("Faites la préparation commune une seule fois, puis suivez uniquement la section de votre lanceur. Galleon détectera ensuite le jeu à partir des mesures MangoHud.") }
            Column {
                objectName: "commonFpsHelp"
                width: parent.width; spacing: 8
                SetupTitle { text: qsTr("Commun · une seule fois") }
                SetupText { text: qsTr("1. Installez MangoHud avec le gestionnaire de logiciels de votre distribution. Pour un lanceur Flatpak, il faut aussi l’extension MangoHud correspondant à son runtime et les accès aux dossiers.") }
                SetupText { text: qsTr("2. Cliquez sur le bouton ci-dessous. Il crée le fichier de configuration et le dossier des mesures, puis applique cette source dans Galleon. Vos autres options MangoHud sont conservées et le fichier existant est sauvegardé avant modification.") }
                Pill {
                    objectName: "configureMangoHudButton"
                    text: qsTr("Configurer MangoHud pour Galleon")
                    onClicked: if (page.widgets.configureMangoHud()) {
                        fpsSource.input.text = page.widgets.recommendedFpsFolder
                        fpsProcess.input.text = ""
                        mangoHudResult.text = qsTr("Configuration prête et appliquée. Passez à la section de votre lanceur.")
                    } else mangoHudResult.text = ""
                }
                SetupText { id: mangoHudResult; visible: text.length > 0; color: "#80e5d1" }
                SetupText { text: qsTr("Configuration : ~/.config/MangoHud/MangoHud.conf (ou votre dossier XDG). Dossier des mesures : ") + page.widgets.recommendedFpsFolder }
                SetupText { text: qsTr("3. Activez Automatique en haut de cette page, puis Appliquer si vous avez changé ce réglage. Une fois votre lanceur configuré, relancez le jeu : les FPS et le mode Jeu doivent apparaître. Sans nouvelles mesures, le mode Jeu se ferme après environ 10 secondes.") }
                SetupText { text: qsTr("Une ancienne variable MANGOHUD_CONFIG ou une configuration propre au jeu peut remplacer le fichier commun. Vérifiez qu’elle utilise le même dossier de mesures. Le bouton configure les fichiers ; il n’installe pas MangoHud et ne modifie pas les lanceurs.") }
            }
            SetupDivider {}
            Column {
                objectName: "flatpakFpsAccess"
                width: parent.width
                spacing: 8
                Row {
                    spacing: 12
                    SetupTitle { text: qsTr("Flatpak · autoriser les CSV FPS") }
                    Pill {
                        objectName: "refreshFlatpakLaunchers"
                        text: qsTr("Actualiser")
                        onClicked: page.flatpakLaunchers = page.widgets.flatpakGameLaunchers()
                    }
                }
                SetupText {
                    text: qsTr("Seuls les lanceurs Flatpak installés apparaissent. Autorisez chaque lanceur utilisé à lire MangoHud.conf et à écrire dans le dossier FPS de Galleon. Aucune autre permission n'est ajoutée.")
                }
                SetupText {
                    visible: page.flatpakLaunchers.length === 0
                    text: qsTr("Aucun Heroic, Bottles, Lutris ou Steam Flatpak détecté.")
                }
                Repeater {
                    model: page.flatpakLaunchers
                    delegate: Row {
                        id: flatpakLauncher
                        required property var modelData
                        spacing: 12
                        Text {
                            width: 140; height: 32; verticalAlignment: Text.AlignVCenter
                            text: flatpakLauncher.modelData.name; color: "white"
                        }
                        Pill {
                            text: qsTr("Autoriser les CSV")
                            onClicked: {
                                page.flatpakError = !page.widgets.allowFlatpakFpsAccess(flatpakLauncher.modelData.id)
                                flatpakResult.text = page.flatpakError ? page.widgets.error
                                    : qsTr("Accès autorisé pour %1. Quittez et relancez ce lanceur.").arg(flatpakLauncher.modelData.name)
                            }
                        }
                    }
                }
                SetupText {
                    id: flatpakResult
                    visible: text.length > 0
                    text: ""
                    color: page.flatpakError ? "#ffb74d" : "#80e5d1"
                }
                SetupText {
                    text: qsTr("Préparez d'abord MangoHud avec le bouton commun. L'extension MangoHud du runtime Flatpak reste nécessaire, ainsi que l'activation du compteur FPS dans le lanceur ou le jeu.")
                }
            }
            SetupDivider {}
            Column {
                objectName: "steamFpsHelp"
                width: parent.width; spacing: 8
                SetupTitle { text: "Steam" }
                SetupText { text: qsTr("1. Bibliothèque → clic droit sur le jeu → Propriétés → Général → Options de lancement.") }
                SetupText { text: qsTr("2. Collez cette ligne dans ce champ, sur une seule ligne (sélectionnez le texte puis Ctrl+C) :") }
                SetupCode { objectName: "steamFpsLaunchCommand"; text: page.widgets.steamLaunchOptions }
                SetupText { text: qsTr("3. Fermez les propriétés et lancez le jeu. Le réglage reste mémorisé pour les prochains lancements.") }
                SetupText { text: qsTr("Cette commande appartient à Steam, jamais au terminal. Si des options existent déjà, conservez-les et ne dupliquez pas %command%. Steam Flatpak demande aussi l’extension MangoHud et les permissions décrites dans le guide Linux.") }
            }
            SetupDivider {}
            Column {
                objectName: "heroicFpsHelp"
                width: parent.width; spacing: 8
                SetupTitle { text: "Heroic" }
                SetupText { text: qsTr("1. Ouvrez les paramètres du jeu et activez MangoHud / Afficher les FPS dans les réglages Linux (l’emplacement dépend de la version).") }
                SetupText { text: qsTr("2. Avec la préparation commune, aucune commande ni variable par jeu n’est nécessaire. Enregistrez les réglages et relancez le jeu. Ne mettez rien dans Arguments du jeu pour MangoHud.") }
                SetupText { text: qsTr("Si vous devez définir les options par jeu : Paramètres du jeu → Avancé → Variables d’environnement. Nom : MANGOHUD_CONFIG. Valeur (sans guillemets supplémentaires) :") }
                SetupCode { text: "autostart_log=1,log_interval=500,output_folder=" + page.widgets.recommendedFpsFolder }
                SetupText { text: qsTr("Heroic Flatpak : installez aussi l’extension MangoHud adaptée et autorisez le fichier de configuration et le dossier des mesures. La case seule ne donne pas ces accès ; voir le guide Linux.") }
            }
            SetupDivider {}
            Column {
                objectName: "lutrisFpsHelp"
                width: parent.width; spacing: 8
                SetupTitle { text: "Lutris" }
                SetupText { text: qsTr("1. Clic droit sur le jeu → Configurer → Options système → activez Compteur de FPS (MangoHud). Affichez les options avancées si nécessaire.") }
                SetupText { text: qsTr("2. Avec la préparation commune, aucune commande ni variable par jeu n’est nécessaire. Enregistrez puis relancez le jeu. Ne mettez pas %command% dans Lutris.") }
                SetupText { text: qsTr("Ancienne version sans cette case : ajoutez seulement mangohud dans Préfixe de commande. Pour des options par jeu : Options système → Variables d’environnement, nom MANGOHUD_CONFIG, valeur :") }
                SetupCode { text: "autostart_log=1,log_interval=500,output_folder=" + page.widgets.recommendedFpsFolder }
            }
            SetupDivider {}
            Column {
                objectName: "polFpsHelp"
                width: parent.width; spacing: 8
                SetupTitle { text: "PlayOnLinux 4" }
                SetupText { text: qsTr("1. Sélectionnez le raccourci du jeu → Configurer → Divers → Commande à exécuter avant de lancer le programme.") }
                SetupText { text: qsTr("2. Ajoutez ces deux lignes dans ce champ, en conservant vos commandes existantes :") }
                SetupCode {
                    objectName: "polFpsLaunchCommand"
                    text: "export " + page.widgets.steamLaunchOptions.replace(" mangohud %command%", "") + "\nexport BEFORE_WINE=\"mangohud${BEFORE_WINE:+ $BEFORE_WINE}\""
                }
                SetupText { text: qsTr("3. Fermez les réglages et relancez le jeu. Ce sont des lignes de pré-lancement, pas des arguments du jeu. Aucun %command% ici.") }
            }
            SetupDivider {}
            Column {
                objectName: "bottlesFpsHelp"
                width: parent.width; spacing: 8
                SetupTitle { text: "Bottles" }
                SetupText { text: qsTr("1. Ouvrez la bouteille du jeu → Préférences → Performances → activez Monitor Performance (MangoHud). Sur certaines versions, cette option est dans Affichage.") }
                SetupText { text: qsTr("2. Avec la préparation commune, aucune commande de lancement n’est nécessaire. Relancez le jeu depuis la bouteille. N’ajoutez pas %command% à ses arguments.") }
                SetupText { text: qsTr("Le bouton commun crée ces options dans MangoHud.conf ; elles sont affichées ici pour vérification, sans saisie supplémentaire nécessaire :") }
                SetupCode { objectName: "bottlesFpsConfig"; text: "autostart_log=1\nlog_interval=500\noutput_folder=" + page.widgets.recommendedFpsFolder }
                SetupText { text: qsTr("Bottles Flatpak : installez l'extension MangoHud correspondant au runtime, puis utilisez le bouton Autoriser les CSV dans la section Flatpak ci-dessus. Quittez et relancez Bottles après autorisation.") }
            }
            SetupDivider {}
            SetupText { text: qsTr("Besoin d’aide ? Le guide MODE_JEU_LINUX.fr.md détaille l’installation Flatpak, les autres activations Steam et les limites OpenGL / Vulkan / DXVK / VKD3D-Proton. Gamescope nécessite une configuration MangoApp distincte.") }
        }
        Text { text: qsTr("Game artwork override (optional)"); color: "white"; font.pixelSize: 18; font.bold: true }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#aebac6"
            text: qsTr("Steam uses cached artwork when the process is identified. For non-Steam games on Windows, add the executable name here to enable detection. The title and image are optional. Click Apply.")
        }
        Row {
            spacing: 12
            Field { id: gameProcess; objectName: "gameProfileProcess"; placeholder: qsTr("Executable (e.g. cs2 or Game.exe)"); maximumLength: 260 }
            Field {
                id: gameTitle; objectName: "gameProfileTitle"; placeholder: qsTr("Game title"); maximumLength: 120
                value: ((page.widgets.gameConfig.profiles || {})[gameProcess.input.text.trim().toLowerCase()] || {}).title || ""
            }
        }
        Field {
            id: gameImage; objectName: "gameProfileImage"; width: Math.min(720, page.width); maximumLength: 4096
            placeholder: qsTr("Absolute local image path")
            value: ((page.widgets.gameConfig.profiles || {})[gameProcess.input.text.trim().toLowerCase()] || {}).image || ""
        }
        Text { width: parent.width; wrapMode: Text.Wrap; text: page.widgets.error; color: "#ffb74d" }
    }
}
