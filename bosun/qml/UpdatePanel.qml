// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: panel
    property var updates: null
    signal closed()
    color: "#aa000000"
    z: 100
    focus: visible
    Keys.onEscapePressed: panel.closed()
    MouseArea { anchors.fill: parent; onClicked: panel.closed() }
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(840, panel.width - 60)
        height: contents.height + 48
        radius: 12
        color: "#202328"
        border.color: "#505760"
        MouseArea { anchors.fill: parent }
        Column {
            id: contents
            x: 24; y: 24; width: parent.width - 48
            spacing: 18
            Text { text: qsTr("GalleonCore updates"); color: "white"; font.pixelSize: 24; font.bold: true }
            Text {
                width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                color: "#c9ced6"; font.pixelSize: 15
                text: panel.updates ? qsTr("Installed: %1 · Latest stable release: %2").arg(panel.updates.installedVersion).arg(panel.updates.latestVersion || qsTr("Unknown")) : ""
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                color: panel.updates && panel.updates.available ? "#9be7a0" : "white"; font.pixelSize: 16
                text: panel.updates ? panel.updates.status : ""
            }
            Text {
                color: "#aeb8c5"; font.pixelSize: 13
                text: panel.updates ? qsTr("Last successful check: %1").arg(panel.updates.lastChecked) : ""
            }
            Pill {
                visible: panel.updates !== null && !panel.updates.packageManaged
                text: panel.updates && panel.updates.automatic ? qsTr("Automatic checks: on") : qsTr("Automatic checks: off")
                selected: panel.updates !== null && panel.updates.automatic
                onClicked: panel.updates.setAutomatic(!panel.updates.automatic)
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap; color: "#c9ced6"; font.pixelSize: 14
                text: qsTr("Automatic checks contact GitHub at most once a day while Bosun is running. Downloads open in your browser; installation remains your choice.")
                visible: panel.updates !== null && !panel.updates.packageManaged
            }
            Pill {
                objectName: "downloadUpdateButton"
                visible: panel.updates !== null && panel.updates.available
                text: panel.updates ? panel.updates.downloadLabel : ""
                selected: true
                onClicked: panel.updates.openDownload()
            }
            Row {
                spacing: 10
                Pill {
                    visible: panel.updates !== null && !panel.updates.packageManaged
                    text: panel.updates && panel.updates.busy ? qsTr("Checking…") : qsTr("Check now")
                    enabledLook: panel.updates !== null && !panel.updates.busy
                    onClicked: panel.updates.checkNow()
                }
                Pill {
                    text: qsTr("Release notes")
                    enabledLook: panel.updates !== null && panel.updates.latestVersion !== ""
                    onClicked: panel.updates.openReleaseNotes()
                }
                Pill { text: qsTr("Website"); onClicked: panel.updates.openWebsite() }
                Pill { text: qsTr("Close"); onClicked: panel.closed() }
            }
        }
    }
}
