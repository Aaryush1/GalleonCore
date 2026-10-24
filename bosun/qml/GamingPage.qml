// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
Flickable {
    id: page
    objectName: "gamingPage"
    required property var widgets
    required property var recording
    property var daemon: null
    property bool menuOpen: false
    property bool obsSettings: false
    property string error: ""
    function showPad() {
        if (!daemon) return
        error = daemon.showGaming()
    }
    Connections {
        target: page.daemon
        function onCommandError(message) { page.error = message }
    }
    clip: true
    contentHeight: content.height
    Column {
        id: content
        width: page.width
        spacing: 16
        Text { text: qsTr("Gaming · Jeu et enregistrement"); color: "white"; font.pixelSize: 24; font.bold: true }
        Text { width: parent.width; wrapMode: Text.Wrap; color: "#b8bec6"; text: qsTr("Automatique : Game Bar sur Windows, OBS sur Linux. Les logiciels indisponibles sont grisés. Le choix est mémorisé immédiatement.") }
        Rectangle {
            width: 320; height: 38; color: "#2e3238"; radius: 5
            Accessible.role: Accessible.ComboBox
            Accessible.name: qsTr("Logiciel d’enregistrement")
            Text { anchors.centerIn: parent; color: "white"; text: (page.recording.choice === "auto" ? qsTr("Automatique") : page.recording.choice === "obs" ? "OBS" : "Game Bar") + "  ▾" }
            MouseArea { anchors.fill: parent; onClicked: page.menuOpen = !page.menuOpen }
        }
        Column {
            visible: page.menuOpen
            spacing: 4
            Repeater {
                model: page.recording.options
                delegate: Pill {
                    required property var modelData
                    width: 320
                    text: modelData.label
                    enabledLook: modelData.available
                    selected: page.recording.choice === modelData.id
                    onClicked: {
                        page.error = page.recording.configure(modelData.id, page.recording.port) ? "" : qsTr("Impossible de mémoriser ce choix.")
                        page.menuOpen = false
                    }
                }
            }
        }
        Text { width: parent.width; wrapMode: Text.Wrap; color: "#80e5d1"; text: qsTr("Logiciel utilisé : ") + (page.recording.backend === "obs" ? "OBS" : "Game Bar") }
        Pill {
            visible: page.recording.backend !== "obs"
            text: qsTr("Configurer OBS")
            onClicked: page.obsSettings = !page.obsSettings
        }
        Column {
            visible: page.recording.backend === "obs" || page.obsSettings
            spacing: 8
            Text { color: "#b8bec6"; text: qsTr("OBS : Outils → Paramètres du serveur WebSocket. Connexion locale uniquement.") }
            Row {
                spacing: 10
                Field { id: portField; width: 110; value: String(page.recording.port); placeholder: "4455"; maximumLength: 5 }
                Field { id: passwordField; width: 320; placeholder: qsTr("Mot de passe OBS (inchangé si vide)"); maximumLength: 1024; input.echoMode: TextInput.Password }
                Pill {
                    text: qsTr("Enregistrer / reconnecter")
                    onClicked: {
                        page.error = page.recording.configure(page.recording.choice, Number(portField.input.text), passwordField.input.text, passwordField.input.text !== "") ? "" : qsTr("Vérifier le port et les préférences.")
                        passwordField.input.text = ""
                    }
                }
                Pill { text: qsTr("Effacer le mot de passe"); onClicked: page.recording.configure(page.recording.choice, page.recording.port, "", true) }
            }
        }
        Text { width: parent.width; wrapMode: Text.Wrap; color: "#b8bec6"; text: page.recording.status }
        Text { visible: page.error !== ""; color: "#ff8a80"; text: page.error }
        Text {
            visible: !page.widgets.gameState.active
            width: parent.width; wrapMode: Text.Wrap; color: "#b8bec6"
            text: qsTr("Les boutons Gaming apparaissent uniquement pendant un jeu détecté, puis disparaissent du choix des touches à sa fermeture.")
        }
        Row {
            visible: !!page.widgets.gameState.active
            spacing: 12
            Text { color: "white"; text: qsTr("Touches du pad · Gaming"); font.pixelSize: 18 }
            Pill {
                objectName: "showGamingOnPad"
                text: qsTr("Afficher sur le pad")
                enabledLook: page.daemon !== null && page.daemon.running
                onClicked: page.showPad()
            }
        }
        Grid {
            objectName: "gamingPadButtons"
            visible: !!page.widgets.gameState.active
            columns: 3; spacing: 10
            Repeater {
                model: page.recording.buttons
                delegate: Rectangle {
                    id: padButton
                    required property var modelData
                    objectName: "gamingPadButton" + modelData.index
                    signal clicked()
                    onClicked: page.recording.trigger(modelData.index)
                    width: 180; height: 90; radius: 8
                    color: modelData.active ? "#285e43" : "#2e3238"
                    opacity: modelData.enabled ? 1 : 0.35
                    Image {
                        anchors.top: parent.top; anchors.topMargin: 8; anchors.horizontalCenter: parent.horizontalCenter
                        width: 42; height: 42
                        visible: parent.modelData.icon !== ""
                        source: visible ? "image://tiles/recording/" + parent.modelData.icon : ""
                        sourceSize.width: 128; sourceSize.height: 128
                    }
                    Text {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 6
                        height: 30; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.Wrap; color: "white"; text: parent.modelData.label; font.pixelSize: 14
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: parent.modelData.enabled
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: padButton.clicked()
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.label
                    Accessible.onPressAction: if (modelData.enabled) padButton.clicked()
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: "#44ffffff" }
        GameModePage {
            width: parent.width
            height: contentHeight
            interactive: false
            widgets: page.widgets
        }

    }
}
