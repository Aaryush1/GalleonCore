// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// Small window to pick a wallpaper from public/wallpaper and send it, with the
// time, to the Galleon's screen. Started by `galleond --preview`.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

Window {
    id: win

    required property var controller
    // The wallpaper being previewed (a catalog name); empty: none yet.
    property string chosen: controller.wallpaper
    property url chosenUrl

    title: qsTr("GalleonCore: screen wallpaper")
    width: 760
    height: 620
    minimumWidth: 760
    minimumHeight: 560
    visible: true
    color: "#1f2023"

    function choose(name, url) {
        chosen = name
        chosenUrl = url
    }

    Component.onCompleted: {
        const list = controller.wallpapers
        for (let i = 0; i < list.length; ++i) {
            if (list[i].name === chosen) {
                chosenUrl = list[i].url
                return
            }
        }
        if (list.length > 0)
            choose(list[0].name, list[0].url)
    }

    Column {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        // What the screen will show. The widget is the very one galleond draws.
        Rectangle {
            width: 720
            height: 384
            color: "black"
            border.color: "#44ffffff"

            Loader {
                id: preview
                anchors.fill: parent
                source: win.controller.widgetUrl
                onLoaded: {
                    item.wallpaper = Qt.binding(() => win.chosenUrl)
                    item.widgetLayout = Qt.binding(() => win.controller.homeWidgets.layout)
                    item.gameState = Qt.binding(() => win.controller.homeWidgets.gameState)
                    item.widgetValues = Qt.binding(() => win.controller.homeWidgets.values)
                }
            }
        }

        Text {
            width: parent.width
            color: "#dddddd"
            font.pixelSize: 14
            elide: Text.ElideRight
            text: win.chosen === "" ? qsTr("No wallpaper found") : win.chosen
        }

        ListView {
            id: thumbs
            width: parent.width
            height: 100
            orientation: ListView.Horizontal
            spacing: 8
            clip: true
            model: win.controller.wallpapers

            delegate: Item {
                id: cell
                required property var modelData
                width: 160
                height: 90

                Image {
                    anchors.fill: parent
                    source: cell.modelData.url
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 320
                    sourceSize.height: 180
                }
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: cell.modelData.name === win.chosen ? 3 : 1
                    border.color: cell.modelData.name === win.controller.wallpaper ? "#4caf50"
                                  : cell.modelData.name === win.chosen ? "#ffffff" : "#66ffffff"
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: win.choose(cell.modelData.name, cell.modelData.url)
                }
                Accessible.role: Accessible.Button
                Accessible.name: cell.modelData.name
            }
        }

        Row {
            spacing: 16

            Rectangle {
                id: apply
                width: 260
                height: 40
                radius: 6
                enabled: win.controller.deckAvailable && win.chosen !== ""
                color: enabled ? "#2e7d32" : "#444444"
                activeFocusOnTab: true

                Text {
                    anchors.centerIn: parent
                    color: apply.enabled ? "white" : "#999999"
                    font.pixelSize: 15
                    text: qsTr("Show on the keyboard")
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: win.controller.SetWallpaper(win.chosen)
                }
                Keys.onReturnPressed: if (enabled) win.controller.SetWallpaper(win.chosen)
                Keys.onSpacePressed: if (enabled) win.controller.SetWallpaper(win.chosen)
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Show on the keyboard")
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: "#bbbbbb"
                font.pixelSize: 14
                text: win.controller.deckAvailable
                      ? qsTr("Galleon module connected")
                      : qsTr("Galleon module not found: preview only")
            }
        }
    }
}
