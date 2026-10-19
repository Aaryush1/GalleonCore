// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// Window to put an application on a key of the Stream Deck module: pick a
// program in the list of installed applications, pick a key, press the button.
// The pages file is edited for you; a running galleond reads it by itself.
// Started by `galleond --apps`.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

Window {
    id: win

    required property var backend
    property var selectedApp: null
    property int selectedKey: -1
    property string status: ""
    property string filter: ""

    title: qsTr("GalleonCore: applications on the keys")
    width: 940
    height: 640
    minimumWidth: 860
    minimumHeight: 560
    visible: true
    color: "#1f2023"

    readonly property var shownApps: {
        const needle = filter.trim().toLowerCase()
        return backend.apps.filter(a => needle === "" || a.name.toLowerCase().indexOf(needle) >= 0)
    }

    // ---- left: the installed applications
    Column {
        id: left
        x: 20
        y: 20
        width: 360
        height: win.height - 40
        spacing: 10

        Text {
            color: "#e6e6e6"
            font.pixelSize: 16
            font.bold: true
            text: qsTr("1. An application")
        }

        Rectangle {
            width: parent.width
            height: 36
            radius: 6
            color: "#2a2d32"
            border.color: search.activeFocus ? "#4caf50" : "#44ffffff"

            TextInput {
                id: search
                anchors.fill: parent
                anchors.margins: 8
                color: "white"
                font.pixelSize: 15
                clip: true
                onTextChanged: win.filter = text
                Accessible.name: qsTr("Search an application")

                Text {
                    visible: search.text === "" && !search.activeFocus
                    color: "#7d8591"
                    font.pixelSize: 15
                    text: qsTr("Search…")
                }
            }
        }

        ListView {
            id: list
            width: parent.width
            height: parent.height - 36 - 30 - 20
            clip: true
            model: win.shownApps
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: row
                required property var modelData
                width: list.width
                height: 44
                radius: 6
                color: win.selectedApp && win.selectedApp.id === modelData.id ? "#2e7d32" : (hover.containsMouse ? "#2a2d32" : "transparent")

                Image {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30
                    height: 30
                    source: "image://themeicon/" + (row.modelData.icon !== "" ? row.modelData.icon : "application-x-executable")
                    sourceSize: Qt.size(60, 60)
                    asynchronous: true
                }
                Text {
                    x: 50
                    width: parent.width - 60
                    anchors.verticalCenter: parent.verticalCenter
                    color: "white"
                    font.pixelSize: 14
                    elide: Text.ElideRight
                    text: row.modelData.name
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: win.selectedApp = row.modelData
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: row.modelData.name
            }
        }
    }

    // ---- right: the keys
    Column {
        x: 410
        y: 20
        width: win.width - 430
        spacing: 12

        Text {
            color: "#e6e6e6"
            font.pixelSize: 16
            font.bold: true
            text: qsTr("2. A key")
        }

        Row {
            spacing: 8
            visible: win.backend.pages.length > 1
            Repeater {
                model: win.backend.pages
                delegate: Rectangle {
                    id: pageButton
                    required property string modelData
                    width: 100
                    height: 30
                    radius: 6
                    color: pageButton.modelData === win.backend.page ? "#2e7d32" : "#2a2d32"
                    Text {
                        anchors.centerIn: parent
                        color: "white"
                        font.pixelSize: 13
                        text: pageButton.modelData
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            win.backend.page = pageButton.modelData
                            win.selectedKey = -1
                        }
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: pageButton.modelData
                }
            }
        }

        Grid {
            columns: 3
            spacing: 10
            Repeater {
                model: win.backend.keys
                delegate: Rectangle {
                    id: keyCell
                    required property var modelData
                    width: 118
                    height: 118
                    radius: 12
                    color: keyCell.modelData.kind === "empty" ? "#16181b" : "#2a2d32"
                    border.width: win.selectedKey === keyCell.modelData.index ? 3 : 1
                    border.color: win.selectedKey === keyCell.modelData.index ? "#4caf50" : "#55ffffff"

                    Image {
                        visible: keyCell.modelData.icon !== ""
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 10
                        width: 56
                        height: 56
                        source: keyCell.modelData.icon !== "" ? "image://themeicon/" + keyCell.modelData.icon : ""
                        sourceSize: Qt.size(112, 112)
                    }
                    Text {
                        width: parent.width - 8
                        x: 4
                        y: keyCell.modelData.icon !== "" ? 72 : 30
                        horizontalAlignment: Text.AlignHCenter
                        color: keyCell.modelData.kind === "app" ? "#9be7a0" : "#e6e6e6"
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        text: keyCell.modelData.kind === "empty" ? qsTr("empty") : keyCell.modelData.label
                    }
                    Text {
                        x: 8
                        y: 6
                        color: "#7d8591"
                        font.pixelSize: 11
                        text: keyCell.modelData.index
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: win.selectedKey = keyCell.modelData.index
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Key %1").arg(keyCell.modelData.index)
                }
            }
        }

        Text {
            visible: win.backend.problem !== ""
            width: parent.width
            color: "#ff8a80"
            font.pixelSize: 13
            wrapMode: Text.Wrap
            text: win.backend.problem
        }

        Row {
            spacing: 12

            Rectangle {
                id: putButton
                width: 280
                height: 40
                radius: 6
                enabled: win.selectedApp !== null && win.selectedKey >= 0 && win.backend.problem === ""
                color: enabled ? "#2e7d32" : "#444444"
                Text {
                    anchors.centerIn: parent
                    color: putButton.enabled ? "white" : "#999999"
                    font.pixelSize: 14
                    elide: Text.ElideRight
                    width: parent.width - 16
                    horizontalAlignment: Text.AlignHCenter
                    text: win.selectedApp !== null && win.selectedKey >= 0
                          ? qsTr("Put “%1” on key %2").arg(win.selectedApp.name).arg(win.selectedKey)
                          : qsTr("3. Choose an application and a key")
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        const error = win.backend.assign(win.selectedApp.id, win.selectedKey)
                        win.status = error === "" ? qsTr("Done. The keys follow within a second if galleond is running.") : error
                    }
                }
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Put the application on the key")
            }

            Rectangle {
                id: clearButton
                width: 150
                height: 40
                radius: 6
                enabled: win.selectedKey >= 0 && win.backend.problem === ""
                color: enabled ? "#5b3a3a" : "#444444"
                Text {
                    anchors.centerIn: parent
                    color: clearButton.enabled ? "white" : "#999999"
                    font.pixelSize: 14
                    text: qsTr("Empty the key")
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        const error = win.backend.clear(win.selectedKey)
                        win.status = error === "" ? qsTr("Key emptied.") : error
                    }
                }
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Empty the key")
            }
        }

        Text {
            width: parent.width
            color: "#bbbbbb"
            font.pixelSize: 13
            wrapMode: Text.Wrap
            text: win.status
        }

        Text {
            width: parent.width
            color: "#7d8591"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            text: qsTr("File: %1 (the previous version is kept as .bak)").arg(win.backend.file)
        }
    }
}
