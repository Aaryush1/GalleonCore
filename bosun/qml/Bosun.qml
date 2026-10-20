// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// Galleon Bosun, the settings window. One tab per theme of the pages file, each
// showing the 12 keys as the module draws them and letting the person change them, then
// a tab to add a theme and the tabs for the keyboard lighting, the wallpapers and the
// screensavers. Every change is written to the pages file at once (with a backup), which
// galleond reads by itself: it reaches the module as soon as it is saved, and stays there
// after a restart.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

Window {
    id: win

    required property var backend
    required property var daemon
    required property var library

    property int currentTab: 0
    property int selectedKey: -1
    property bool cropOpen: false // only for a picture of the crop window (--crop)
    // With the icon in the notification area, closing the window only hides it.
    property bool hideOnClose: false
    // The page shown inside a theme's tab: its own, or one of the sub-pages it leads to.
    property string shownPage: ""
    // What the last edit said, in the status bar.
    property string message: ""
    property bool messageIsError: false
    // The last theme looked at: its colour is the keyboard tab's (the left dial of the module shows it).
    property int lastTheme: 0

    onClosing: close => {
        if (win.hideOnClose) {
            close.accepted = false
            win.visible = false
        }
    }

    title: qsTr("Galleon Bosun")
    width: 1240
    height: 740
    minimumWidth: 1000
    minimumHeight: 700
    visible: true
    color: "#1b1c1f"

    // One tab per theme, a tab to add one, then the fixed ones.
    readonly property var tabs: backend.themes.map(t => ({title: t.name, kind: "theme", page: t.page, color: t.color})).concat([
        {title: "+", kind: "newtab", page: "", color: "#9aa0a6"},
        {title: qsTr("Keyboard"), kind: "keyboard", page: "", color: lastTheme < backend.themes.length ? backend.themes[lastTheme].color : "#9aa0a6"},
        {title: qsTr("Wallpapers"), kind: "wallpapers", page: "", color: "#ff00ff"},
        {title: qsTr("Screensavers"), kind: "screensavers", page: "", color: "#b388ff"}
    ])
    readonly property var tab: tabs[Math.min(currentTab, tabs.length - 1)]
    readonly property string page: tab.kind === "theme" ? (shownPage !== "" && backend.pageNames.indexOf(shownPage) >= 0 ? shownPage : tab.page) : ""
    // The sub-pages that the keys of this tab's page lead to (not the pages of other tabs).
    readonly property var subPages: tab.kind === "theme" ? backend.subPagesOf(tab.page) : []
    readonly property var keys: tab.kind === "theme" ? backend.keys(page) : []
    readonly property var selected: selectedKey >= 0 && selectedKey < keys.length ? keys[selectedKey] : null

    // What an edit answered, for the status bar.
    function answered(error, done) {
        message = error === "" ? done : error
        messageIsError = error !== ""
    }

    // Another tab: the selection and the sub-page are forgotten. (Not when the list of tabs is
    // read again after an edit: the person is still on the same key.)
    onCurrentTabChanged: {
        selectedKey = -1
        shownPage = ""
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
                width: Math.max(90, (win.width - 40 - (win.tabs.length - 1) * 4) / win.tabs.length)
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

    // ---- a theme: the 12 keys, and on the right what the selected key does or the tab itself
    Item {
        id: themePage
        visible: win.tab.kind === "theme" && win.backend.problem === ""
        x: 30
        y: 84
        width: win.width - 60
        height: win.height - 104 - statusBar.height

        // The pages of the tab: its own and the sub-pages that keys lead to.
        Row {
            id: pageChoice
            spacing: 6
            Text {
                height: 30
                verticalAlignment: Text.AlignVCenter
                color: "#9aa0a6"
                font.pixelSize: 13
                text: qsTr("Page")
            }
            Pill {
                text: win.tab.page
                selected: win.page === win.tab.page
                onClicked: win.shownPage = win.tab.page
            }
            Repeater {
                model: win.subPages
                delegate: Pill {
                    required property string modelData
                    text: modelData
                    selected: win.page === modelData
                    onClicked: {
                        win.shownPage = modelData
                        win.selectedKey = -1
                    }
                }
            }
        }
        Text {
            id: pageNote
            y: pageChoice.height + 4
            width: keyGrid.width
            color: "#9aa0a6"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            text: {
                const fill = win.backend.autoFill(win.page)
                return fill === "" ? "" : qsTr("The module also adds: %1.").arg(fill)
            }
        }

        Grid {
            id: keyGrid
            y: pageChoice.height + pageNote.height + 12
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
                        source: "image://tiles/" + win.page + "/" + keyCell.modelData.index + "?r=" + win.backend.revision + "&theme=" + win.tab.page
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

        // The right side: the key that is selected, else the tab itself.
        Flickable {
            id: rightSide
            x: keyGrid.width + 40
            width: parent.width - x
            height: parent.height
            contentWidth: width
            contentHeight: rightColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: rightColumn
                width: rightSide.width - 12
                spacing: 22

                KeyEditor {
                    visible: win.selected !== null
                    width: parent.width
                    backend: win.backend
                    page: win.page
                    key: win.selected !== null ? win.selected : ({index: 0, empty: true, type: "none"})
                    onAnswered: (error, done) => win.answered(error, done)
                }
                Text {
                    visible: win.selected === null
                    color: "#9aa0a6"
                    font.pixelSize: 14
                    text: qsTr("Choose a key to change what it does and shows.")
                }
                Rectangle {
                    width: parent.width
                    height: 1
                    color: "#33ffffff"
                }
                TabEditor {
                    width: parent.width
                    backend: win.backend
                    index: Math.min(win.currentTab, win.backend.themes.length - 1)
                    theme: win.backend.themes[Math.min(win.currentTab, win.backend.themes.length - 1)]
                    onAnswered: (error, done) => win.answered(error, done)
                    onMoved: to => win.currentTab = to
                    onRemoved: win.currentTab = Math.max(0, win.currentTab - 1)
                }
            }
        }
    }

    // ---- a new tab
    NewTab {
        visible: win.tab.kind === "newtab" && win.backend.problem === ""
        x: 30
        y: 90
        width: Math.min(win.width - 60, 560)
        backend: win.backend
        onAnswered: (error, done) => win.answered(error, done)
        onCreated: win.currentTab = win.backend.themes.length - 1
    }

    // ---- the keyboard
    Column {
        visible: win.tab.kind === "keyboard" && win.backend.problem === ""
        x: 30
        y: 90
        width: Math.min(win.width - 60, 760)
        spacing: 12

        Text {
            color: "white"
            font.pixelSize: 18
            font.bold: true
            text: qsTr("Keyboard lighting")
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            color: "#c9ced6"
            font.pixelSize: 14
            text: qsTr("Corsair's own page, the Web Hub, sets the keyboard's lighting and keys in your browser. It needs a Chromium-based browser (Chrome, Chromium, Edge, Opera).")
        }
        Pill {
            selected: true
            text: qsTr("Open the Corsair Web Hub in your browser")
            onClicked: Qt.openUrlExternally("https://www.corsair.com/web-hub")
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            color: "#9aa0a6"
            font.pixelSize: 13
            text: qsTr("It opens in your browser, not in this window. Careful: what the Web Hub saves into the keyboard's own memory is the keyboard's business, and this application never writes there. The screen, the 12 keys and the dials of the module are set from the other tabs.")
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            color: "#7d8591"
            font.pixelSize: 13
            text: qsTr("The lighting can also be driven from Linux with OpenRGB (the dials' rings already are, through this project's plugin); the per-key lighting of the keyboard is not available there yet.")
        }
    }

    WallpapersPage {
        visible: win.tab.kind === "wallpapers" && win.backend.problem === ""
        x: 30
        y: 84
        width: win.width - 60
        height: win.height - 104 - statusBar.height
        library: win.library
        backend: win.backend
    }

    ScreensaversPage {
        visible: win.tab.kind === "screensavers" && win.backend.problem === ""
        x: 30
        y: 84
        width: win.width - 60
        height: win.height - 104 - statusBar.height
        backend: win.backend
        cropAtStart: win.cropOpen
    }

    // ---- the status bar: does the module follow, and what was done
    Rectangle {
        id: statusBar
        x: 0
        y: win.height - height
        width: win.width
        height: 34
        color: "#15171a"
        Rectangle {
            width: parent.width
            height: 1
            color: "#33ffffff"
        }
        Row {
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 10
                radius: 5
                color: win.daemon.running ? "#43a047" : "#ffb74d"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: "#c9ced6"
                font.pixelSize: 13
                text: win.daemon.running ? qsTr("galleond is running: a change reaches the module at once.")
                                         : qsTr("galleond is not running: changes are saved and will show when it starts.")
            }
            Pill {
                visible: !win.daemon.running
                height: 24
                text: qsTr("Start it")
                onClicked: win.answered(win.daemon.start(), qsTr("Starting galleond…"))
            }
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width / 2.4
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            color: win.messageIsError ? "#ff8a80" : "#9be7a0"
            font.pixelSize: 13
            text: win.message
        }
    }
}
