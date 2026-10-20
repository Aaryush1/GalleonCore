// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// Galleon Bosun, the settings window. One tab per theme of the pages file, each
// showing the 12 keys as the module draws them, then the tabs for the keyboard
// lighting, the wallpapers and the screensavers. Only the tabs and the keys are
// built so far: nothing here writes the pages file yet.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

Window {
    id: win

    required property var backend

    property int currentTab: 0
    property int selectedKey: -1
    // The last theme looked at: its colour is the keyboard tab's (the left dial of the module shows it).
    property int lastTheme: 0

    title: qsTr("Galleon Bosun")
    width: 1240
    height: 740
    minimumWidth: 1000
    minimumHeight: 700
    visible: true
    color: "#1b1c1f"

    // One tab per theme, then the fixed ones.
    readonly property var tabs: backend.themes.map(t => ({title: t.name, kind: "theme", page: t.page, color: t.color})).concat([
        {title: qsTr("Keyboard"), kind: "keyboard", page: "", color: lastTheme < backend.themes.length ? backend.themes[lastTheme].color : "#9aa0a6"},
        {title: qsTr("Wallpapers"), kind: "wallpapers", page: "", color: "#ff00ff"},
        {title: qsTr("Screensavers"), kind: "screensavers", page: "", color: "#b388ff"}
    ])
    readonly property var tab: tabs[Math.min(currentTab, tabs.length - 1)]
    readonly property var keys: tab.kind === "theme" ? backend.keys(tab.page) : []
    readonly property var selected: selectedKey >= 0 && selectedKey < keys.length ? keys[selectedKey] : null

    onTabChanged: selectedKey = -1
    onCurrentTabChanged: {
        if (currentTab < backend.themes.length)
            lastTheme = currentTab
    }

    // ---- the tab bar
    Row {
        id: bar
        x: 20
        y: 16
        height: 44
        spacing: 4

        Repeater {
            model: win.tabs
            delegate: Rectangle {
                id: tabButton
                required property var modelData
                required property int index
                readonly property bool current: tabButton.index === win.currentTab
                width: Math.max(120, (win.width - 40 - (win.tabs.length - 1) * 4) / win.tabs.length)
                height: bar.height
                radius: 8
                color: tabButton.current ? "#2a2d32" : (hover.containsMouse ? "#232529" : "transparent")

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 16
                    height: 3
                    radius: 1.5
                    color: tabButton.modelData.color
                    opacity: tabButton.current ? 1 : 0.45
                }
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 8
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    color: tabButton.current ? "white" : "#b8bec6"
                    font.pixelSize: 14
                    text: tabButton.modelData.title
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: win.currentTab = tabButton.index
                }
                Accessible.role: Accessible.PageTab
                Accessible.name: tabButton.modelData.title
            }
        }
    }

    Rectangle {
        x: 20
        y: 64
        width: win.width - 40
        height: 1
        color: "#33ffffff"
    }

    // ---- a problem with the pages file hides everything else
    Text {
        visible: win.backend.problem !== ""
        x: 30
        y: 90
        width: win.width - 60
        color: "#ff8a80"
        font.pixelSize: 14
        wrapMode: Text.Wrap
        text: win.backend.problem
    }

    // ---- a theme: the 12 keys and the detail of one
    Item {
        id: themePage
        visible: win.tab.kind === "theme" && win.backend.problem === ""
        x: 30
        y: 84
        width: win.width - 60
        height: win.height - 104

        Text {
            id: pageNote
            width: keyGrid.width
            color: "#9aa0a6"
            font.pixelSize: 13
            wrapMode: Text.Wrap
            text: {
                const fill = win.backend.autoFill(win.tab.page)
                return fill === "" ? qsTr("Page “%1”").arg(win.tab.page)
                                   : qsTr("Page “%1”. The module also adds: %2.").arg(win.tab.page).arg(fill)
            }
        }

        Grid {
            id: keyGrid
            y: pageNote.height + 12
            columns: 3
            spacing: 12

            Repeater {
                model: win.keys
                delegate: Rectangle {
                    id: keyCell
                    required property var modelData
                    width: 140
                    height: 140
                    radius: 14
                    color: "transparent"
                    border.width: win.selectedKey === keyCell.modelData.index ? 3 : 0
                    border.color: win.tab.color

                    Image {
                        anchors.fill: parent
                        anchors.margins: win.selectedKey === keyCell.modelData.index ? 3 : 0
                        asynchronous: false
                        cache: false
                        smooth: true
                        source: "image://tiles/" + win.tab.page + "/" + keyCell.modelData.index + "?r=" + win.backend.revision
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: win.selectedKey = keyCell.modelData.index
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: keyCell.modelData.empty ? qsTr("Key %1, empty").arg(keyCell.modelData.index + 1)
                                                             : qsTr("Key %1, %2").arg(keyCell.modelData.index + 1).arg(keyCell.modelData.label)
                }
            }
        }

        // The detail of the selected key.
        Column {
            x: keyGrid.width + 40
            y: pageNote.height + 12
            width: parent.width - x
            spacing: 14

            Text {
                color: "white"
                font.pixelSize: 18
                font.bold: true
                text: win.selected ? qsTr("Key %1").arg(win.selected.index + 1) : qsTr("Choose a key")
            }
            Repeater {
                model: win.selected && !win.selected.empty
                       ? [[qsTr("Shows"), win.selected.label], [qsTr("Does"), win.selected.kind], [qsTr("Target"), win.selected.detail]]
                       : []
                delegate: Row {
                    id: detailRow
                    required property var modelData
                    visible: detailRow.modelData[1] !== ""
                    spacing: 12
                    Text {
                        width: 80
                        color: "#9aa0a6"
                        font.pixelSize: 14
                        text: detailRow.modelData[0]
                    }
                    Text {
                        width: 360
                        color: "white"
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                        text: detailRow.modelData[1]
                    }
                }
            }
            Text {
                visible: win.selected !== null && win.selected.empty
                color: "#9aa0a6"
                font.pixelSize: 14
                text: qsTr("This key is empty.")
            }
            Text {
                width: parent.width
                color: "#7d8591"
                font.pixelSize: 13
                wrapMode: Text.Wrap
                text: qsTr("Editing is not built yet: this window only shows the keys. To put an application on a key for now, run “galleond --apps”.")
            }
            Text {
                width: parent.width
                color: "#7d8591"
                font.pixelSize: 12
                wrapMode: Text.Wrap
                text: qsTr("File: %1").arg(win.backend.file)
            }
        }
    }

    // ---- the tabs still to build
    Column {
        visible: win.tab.kind !== "theme" && win.backend.problem === ""
        x: 30
        y: 90
        width: win.width - 60
        spacing: 10

        Text {
            color: "white"
            font.pixelSize: 18
            font.bold: true
            text: win.tab.title
        }
        Text {
            width: parent.width
            color: "#9aa0a6"
            font.pixelSize: 14
            wrapMode: Text.Wrap
            text: {
                switch (win.tab.kind) {
                case "keyboard":
                    return qsTr("Planned: a link to the Corsair Web Hub, opened in your browser. The keyboard's own lighting is not controlled from Linux yet (see the OpenRGB plugin).")
                case "wallpapers":
                    return qsTr("Planned: thumbnails of the wallpapers, adding your own pictures, and cropping them to the screen (720 × 384).")
                default:
                    return qsTr("Planned: the screensavers, adding a video with its resolution and frame rate shown, cropping it on the module's real geometry, and converting it with FFmpeg in the background.")
                }
            }
        }
    }
}
