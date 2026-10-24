// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
Flickable {
    id: page
    objectName: "gameModePage"
    required property var widgets
    property bool advanced: false
    contentWidth: width
    contentHeight: content.height
    clip: true
    function shellQuote(value) { return "'" + value.split("'").join("'\\''") + "'" }
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
                text: page.widgets.captureStatus.running ? qsTr("Arrêter la capture FPS")
                    : page.widgets.captureStatus.installed ? qsTr("Démarrer la capture FPS") : qsTr("Installer PresentMon")
                enabledLook: !page.widgets.captureStatus.starting && page.widgets.captureStatus.state !== "offline"
                onClicked: {
                    if (!page.widgets.captureStatus.installed) Qt.openUrlExternally("https://github.com/GameTechDev/PresentMon/releases/latest")
                    else page.widgets.requestGameCapture(!page.widgets.captureStatus.running)
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
            visible: !page.widgets.windowsCapture
            width: parent.width; spacing: 8
            Text { text: qsTr("Linux · CSV MangoHud"); color: "white"; font.pixelSize: 18; font.bold: true }
            Text { width: parent.width; wrapMode: Text.Wrap; color: "#b8bec6"; text: qsTr("Dossier utilisateur conseillé : ") + page.widgets.recommendedFpsFolder }
            Pill {
                text: qsTr("Préparer et sélectionner ce dossier")
                onClicked: if (page.widgets.prepareFpsFolder()) {
                    fpsSource.input.text = page.widgets.recommendedFpsFolder
                    fpsProcess.input.text = ""
                }
            }
            Pill {
                objectName: "configureMangoHudButton"
                text: qsTr("Créer / configurer MangoHud et le dossier CSV")
                onClicked: if (page.widgets.configureMangoHud()) {
                    fpsSource.input.text = page.widgets.recommendedFpsFolder
                    fpsProcess.input.text = ""
                    mangoHudResult.text = qsTr("MangoHud configuré et source FPS appliquée dans Galleon. Les autres réglages sont conservés ; une sauvegarde est créée si le fichier existant est modifié. Activez MangoHud dans le lanceur puis relancez le jeu. Les permissions Flatpak restent nécessaires.")
                } else mangoHudResult.text = ""
            }
            Text { id: mangoHudResult; width: parent.width; wrapMode: Text.Wrap; color: "#80e5d1" }
            Text { width: parent.width; wrapMode: Text.Wrap; color: "#b8bec6"; text: qsTr("Cliquez ensuite sur Appliquer. Dans Steam : Bibliothèque → clic droit sur le jeu → Propriétés → Général → Options de lancement. Collez la ligne suivante (Ctrl+C pour copier) :") }
            TextEdit {
                objectName: "steamFpsLaunchCommand"
                width: parent.width; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                color: "#80e5d1"; font.pixelSize: 14
                text: page.widgets.steamLaunchOptions
            }
            Text { width: parent.width; wrapMode: Text.Wrap; color: "#b8bec6"; text: qsTr("%command% appartient uniquement à Steam, jamais au terminal. Heroic : Paramètres du jeu → Avancé → Variables d’environnement ; Lutris : Configurer le jeu → Options système → Variables d’environnement. Activez MangoHud dans le lanceur. Si vous avez utilisé le bouton de configuration ci-dessus, aucune variable supplémentaire n’est nécessaire (sauf configuration existante qui remplace le fichier commun). Sinon ajoutez MANGOHUD_CONFIG avec cette valeur :") }
            TextEdit {
                width: parent.width; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                color: "#80e5d1"; font.pixelSize: 14
                text: "autostart_log=1,log_interval=500,output_folder=" + page.widgets.recommendedFpsFolder
            }
            Text { width: parent.width; wrapMode: Text.Wrap; color: "#b8bec6"; text: qsTr("PlayOnLinux 4 : sélectionnez le raccourci du jeu → Configurer → Divers → Commande à exécuter avant de lancer le programme. Ajoutez ces deux lignes en conservant vos autres commandes :") }
            TextEdit {
                objectName: "polFpsLaunchCommand"
                width: parent.width; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                color: "#80e5d1"; font.pixelSize: 14
                text: "export " + page.widgets.steamLaunchOptions.replace(" mangohud %command%", "") + "\nexport BEFORE_WINE=\"mangohud${BEFORE_WINE:+ $BEFORE_WINE}\""
            }
            Text { width: parent.width; wrapMode: Text.Wrap; color: "#b8bec6"; text: qsTr("Bottles : bouteille du jeu → Préférences → Performances → Monitor Performance (MangoHud), ou Affichage selon la version. Activez la surveillance. Dans ~/.config/MangoHud/MangoHud.conf, ajoutez ces lignes sans effacer les autres réglages ; créez le dossier ~/.config/MangoHud si nécessaire :") }
            TextEdit {
                objectName: "bottlesFpsConfig"
                width: parent.width; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                color: "#80e5d1"; font.pixelSize: 14
                text: "autostart_log=1\nlog_interval=500\noutput_folder=" + page.widgets.recommendedFpsFolder
            }
            Text { width: parent.width; wrapMode: Text.Wrap; color: "#b8bec6"; text: qsTr("Bottles Flatpak : utilisez l’extension MangoHud adaptée à son runtime. Dans un terminal, autorisez le fichier de configuration et le dossier CSV avec cette commande, puis quittez et relancez Bottles :") }
            TextEdit {
                objectName: "bottlesFlatpakFpsCommand"
                width: parent.width; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                color: "#80e5d1"; font.pixelSize: 14
                text: "flatpak override --user --filesystem=\"$HOME/.config/MangoHud:ro\" --filesystem=" + page.shellQuote(page.widgets.recommendedFpsFolder) + " --env=MANGOHUD_CONFIGFILE=\"$HOME/.config/MangoHud/MangoHud.conf\" com.usebottles.bottles"
            }
            Text { width: parent.width; wrapMode: Text.Wrap; color: "#b8bec6"; text: qsTr("Bottles et POL : aucun %command% dans les paramètres ni dans les arguments du jeu. Gamescope demande une configuration MangoApp distincte.") }
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
