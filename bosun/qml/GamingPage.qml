// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
Flickable {
    id: page
    objectName: "gamingPage"
    required property var recording
    property bool menuOpen: false
    property bool obsSettings: false
    property string error: ""
    clip: true
    contentHeight: content.height
    Column {
        id: content
        width: page.width
        spacing: 16
        Text { text: qsTr("Gaming · Enregistrement"); color: "white"; font.pixelSize: 24; font.bold: true }
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
        Text { color: "white"; text: qsTr("Touches du pad · thème Gaming (sélection avec la molette)"); font.pixelSize: 18 }
        Grid {
            columns: 3; spacing: 10
            Repeater {
                model: page.recording.buttons
                delegate: Rectangle {
                    required property var modelData
                    width: 180; height: 60; radius: 8
                    color: modelData.active ? "#285e43" : "#2e3238"
                    opacity: modelData.enabled ? 1 : 0.35
                    Text { anchors.fill: parent; anchors.margins: 8; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; wrapMode: Text.Wrap; color: "white"; text: parent.modelData.label; font.pixelSize: 16 }
                }
            }
        }
    }
}
