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

    property int currentTab: 0
    property int selectedKey: -1
    // The page shown inside a theme's tab: its own, or one of the sub-pages it leads to.
    property string shownPage: ""
    // What the last edit said, in the status bar.
    property string message: ""
    property bool messageIsError: false
    // The last theme looked at: its colour is the keyboard tab's (the left dial of the module shows it).
    property int lastTheme: 0

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
    // The pages that are not the page of a theme: reached from a key.
    readonly property var subPages: backend.pageNames.filter(name => !backend.themes.some(t => t.page === name))
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

    // ---- the tabs still to build
    Column {
        visible: (win.tab.kind === "keyboard" || win.tab.kind === "wallpapers") && win.backend.problem === ""
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
                default:
                    return qsTr("Planned: thumbnails of the wallpapers, adding your own pictures, and cropping them to the screen (720 × 384).")
                }
            }
        }
    }

    // ---- the screensavers: the settings, then the videos with what is known of each
    Flickable {
        id: saverPage
        visible: win.tab.kind === "screensavers" && win.backend.problem === ""
        readonly property var saver: win.backend.screensaver
        property string message: ""
        property bool messageIsError: false
        x: 30
        y: 84
        width: win.width - 60
        height: win.height - 104 - statusBar.height
        contentWidth: width
        contentHeight: saverColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        // What an edit answered: nothing when it was done, else why not.
        function answered(error, done) {
            message = error === "" ? done : error
            messageIsError = error !== ""
        }

        Column {
            id: saverColumn
            width: saverPage.width
            spacing: 14

            Text {
                color: "white"
                font.pixelSize: 18
                font.bold: true
                text: qsTr("Screensavers")
            }
            Text {
                width: parent.width
                color: saverPage.saver.enabled ? "#9be7a0" : "#ffb74d"
                font.pixelSize: 14
                wrapMode: Text.Wrap
                text: {
                    if (!saverPage.saver.hasGeometry)
                        return qsTr("Off: the measures of the module (screen, keys, gaps) are missing from the pages file, so a video cannot be spread over it.")
                    if (saverPage.saver.videos.length === 0)
                        return qsTr("Off: no video.")
                    return qsTr("On.")
                }
            }

            // The settings.
            Grid {
                columns: 2
                columnSpacing: 20
                rowSpacing: 12
                verticalItemAlignment: Grid.AlignVCenter

                Text {
                    color: "#c9ced6"
                    font.pixelSize: 14
                    text: qsTr("Starts after")
                }
                Row {
                    spacing: 14
                    Stepper {
                        value: saverPage.saver.idleMinutes
                        from: 1
                        to: 720
                        unit: qsTr("min")
                        onPicked: v => saverPage.answered(win.backend.setIdleMinutes(v), qsTr("Saved."))
                    }
                    Repeater {
                        model: [1, 2, 5, 10, 30, 60]
                        delegate: Pill {
                            required property int modelData
                            text: modelData
                            selected: saverPage.saver.idleMinutes === modelData
                            onClicked: saverPage.answered(win.backend.setIdleMinutes(modelData), qsTr("Saved."))
                        }
                    }
                }

                Text {
                    color: "#c9ced6"
                    font.pixelSize: 14
                    text: qsTr("Images per second")
                }
                Row {
                    spacing: 14
                    Stepper {
                        value: saverPage.saver.fps
                        from: 1
                        to: 20
                        onPicked: v => saverPage.answered(win.backend.setFps(v), qsTr("Saved."))
                    }
                    Text {
                        height: 32
                        verticalAlignment: Text.AlignVCenter
                        color: "#7d8591"
                        font.pixelSize: 12
                        text: qsTr("The module shows about 20 at most, screen and keys together.")
                    }
                }

                Text {
                    color: "#c9ced6"
                    font.pixelSize: 14
                    text: qsTr("While a track plays")
                }
                Row {
                    spacing: 8
                    Pill {
                        text: qsTr("Never")
                        selected: !saverPage.saver.whilePlaying
                        onClicked: saverPage.answered(win.backend.setWhilePlaying(false), qsTr("Saved."))
                    }
                    Pill {
                        text: qsTr("Allowed")
                        selected: saverPage.saver.whilePlaying
                        onClicked: saverPage.answered(win.backend.setWhilePlaying(true), qsTr("Saved."))
                    }
                }
            }

            Text {
                width: parent.width
                visible: saverPage.message !== ""
                color: saverPage.messageIsError ? "#ff8a80" : "#9be7a0"
                font.pixelSize: 13
                wrapMode: Text.Wrap
                text: saverPage.message
            }
            Text {
                visible: saverPage.saver.probeMissing
                width: parent.width
                color: "#ffb74d"
                font.pixelSize: 13
                wrapMode: Text.Wrap
                text: qsTr("ffprobe was not found (it comes with FFmpeg): the size, rate and length of the videos cannot be shown.")
            }
            Text {
                visible: saverPage.saver.canvas !== ""
                width: parent.width
                color: "#9aa0a6"
                font.pixelSize: 13
                wrapMode: Text.Wrap
                text: qsTr("A video is cut to %1 pixels: the screen and the 12 keys seen as one picture.").arg(saverPage.saver.canvas)
            }

            Repeater {
                model: saverPage.saver.videos
                delegate: Rectangle {
                    id: videoRow
                    required property var modelData
                    property bool confirming: false
                    width: saverColumn.width
                    height: Math.max(150, info.height + 28)
                    radius: 10
                    color: "#23252a"

                    Timer {
                        id: forget
                        interval: 4000
                        onTriggered: videoRow.confirming = false
                    }

                    Rectangle {
                        id: pictureFrame
                        x: 14
                        y: 14
                        width: 84
                        height: 122
                        radius: 6
                        color: "#15171a"
                        Image {
                            anchors.fill: parent
                            visible: videoRow.modelData.thumbnail !== ""
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                            source: videoRow.modelData.thumbnail
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: videoRow.modelData.thumbnail === ""
                            color: "#5f6670"
                            font.pixelSize: 12
                            text: videoRow.modelData.found ? qsTr("…") : qsTr("none")
                        }
                    }
                    Column {
                        id: info
                        x: 114
                        y: 14
                        width: parent.width - 130 - removeButton.width
                        spacing: 6

                        // The title shown while the screensaver runs; empty: the name of the file.
                        Rectangle {
                            width: parent.width
                            height: 34
                            radius: 6
                            color: "#16181b"
                            border.width: 1
                            border.color: title.activeFocus ? "#4caf50" : "#44ffffff"
                            TextInput {
                                id: title
                                anchors.fill: parent
                                anchors.margins: 8
                                color: "white"
                                font.pixelSize: 15
                                font.bold: true
                                clip: true
                                maximumLength: 60
                                text: videoRow.modelData.title
                                onEditingFinished: {
                                    if (text.trim() !== videoRow.modelData.title)
                                        saverPage.answered(win.backend.setVideoTitle(videoRow.modelData.index, text), qsTr("Title saved."))
                                }
                                Accessible.name: qsTr("Title of the video")
                                Text {
                                    visible: title.text === "" && !title.activeFocus
                                    color: "#7d8591"
                                    font.pixelSize: 15
                                    text: videoRow.modelData.name
                                }
                            }
                        }
                        Text {
                            width: parent.width
                            elide: Text.ElideMiddle
                            color: "#7d8591"
                            font.pixelSize: 12
                            text: videoRow.modelData.file
                        }
                        Text {
                            visible: videoRow.modelData.details !== ""
                            width: parent.width
                            color: "#c9ced6"
                            font.pixelSize: 14
                            text: videoRow.modelData.details
                        }
                        Repeater {
                            model: videoRow.modelData.notes
                            delegate: Text {
                                required property string modelData
                                width: info.width
                                color: "#ffb74d"
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                                text: "• " + modelData
                            }
                        }
                    }
                    Pill {
                        id: removeButton
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        y: 14
                        danger: true
                        text: videoRow.confirming ? qsTr("Click again to remove") : qsTr("Remove")
                        onClicked: {
                            if (!videoRow.confirming) {
                                videoRow.confirming = true
                                forget.restart()
                                return
                            }
                            saverPage.answered(win.backend.removeVideo(videoRow.modelData.index),
                                               saverPage.saver.videos.length <= 1 ? qsTr("Removed: no video is left, the screensaver is off.") : qsTr("Removed from the list (the file is kept)."))
                        }
                    }
                }
            }

            // ---- adding a video: choose it (drop it or give its path), look at it, then add it
            Rectangle {
                id: addBox
                readonly property var candidate: win.backend.candidate
                readonly property bool chosen: candidate.path !== undefined || candidate.name !== undefined
                width: parent.width
                height: addColumn.height + 28
                radius: 10
                color: drop.containsDrag ? "#1f3a2a" : "#1f2125"
                border.width: 1
                border.color: drop.containsDrag ? "#4caf50" : "#33ffffff"

                DropArea {
                    id: drop
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: event => {
                        if (event.hasUrls && event.urls.length > 0) {
                            win.backend.inspect(event.urls[0].toString())
                            saverPage.message = ""
                        }
                    }
                }

                Column {
                    id: addColumn
                    x: 14
                    y: 14
                    width: parent.width - 28
                    spacing: 10

                    Text {
                        color: "white"
                        font.pixelSize: 16
                        font.bold: true
                        text: qsTr("Add a video")
                    }
                    Text {
                        width: parent.width
                        color: "#9aa0a6"
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                        text: qsTr("Drop a video file here, or write its path. It is only looked at until you add it, and added as it is, where it is: nothing is copied or converted yet.")
                    }
                    Row {
                        spacing: 10
                        Rectangle {
                            width: addColumn.width - checkButton.width - 10
                            height: 34
                            radius: 6
                            color: "#16181b"
                            border.width: 1
                            border.color: pathInput.activeFocus ? "#4caf50" : "#44ffffff"
                            TextInput {
                                id: pathInput
                                anchors.fill: parent
                                anchors.margins: 8
                                color: "white"
                                font.pixelSize: 14
                                clip: true
                                selectByMouse: true
                                onAccepted: checkButton.clicked()
                                Accessible.name: qsTr("Path of a video file")
                                Text {
                                    visible: pathInput.text === "" && !pathInput.activeFocus
                                    color: "#7d8591"
                                    font.pixelSize: 14
                                    text: qsTr("/path/to/video.mp4")
                                }
                            }
                        }
                        Pill {
                            id: checkButton
                            text: qsTr("Look at it")
                            enabledLook: pathInput.text.trim() !== ""
                            onClicked: {
                                win.backend.inspect(pathInput.text)
                                saverPage.message = ""
                            }
                        }
                    }

                    // The video being looked at.
                    Rectangle {
                        visible: addBox.chosen
                        width: parent.width
                        height: Math.max(150, candidateInfo.height + 28)
                        radius: 8
                        color: "#23252a"

                        Rectangle {
                            x: 14
                            y: 14
                            width: 84
                            height: 122
                            radius: 6
                            color: "#15171a"
                            Image {
                                anchors.fill: parent
                                visible: addBox.candidate.thumbnail !== undefined && addBox.candidate.thumbnail !== ""
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: false
                                source: addBox.candidate.thumbnail !== undefined ? addBox.candidate.thumbnail : ""
                            }
                        }
                        Column {
                            id: candidateInfo
                            x: 114
                            y: 14
                            width: parent.width - 130
                            spacing: 6
                            Text {
                                width: parent.width
                                elide: Text.ElideMiddle
                                color: "white"
                                font.pixelSize: 16
                                font.bold: true
                                text: addBox.candidate.name !== undefined ? addBox.candidate.name : ""
                            }
                            Text {
                                visible: text !== ""
                                width: parent.width
                                color: "#c9ced6"
                                font.pixelSize: 14
                                text: addBox.candidate.state === "checking" ? qsTr("Reading the video…")
                                      : (addBox.candidate.details !== undefined ? addBox.candidate.details : "")
                            }
                            Repeater {
                                model: addBox.candidate.notes !== undefined ? addBox.candidate.notes : []
                                delegate: Text {
                                    required property string modelData
                                    width: candidateInfo.width
                                    color: "#ffb74d"
                                    font.pixelSize: 13
                                    wrapMode: Text.Wrap
                                    text: "• " + modelData
                                }
                            }
                            Text {
                                visible: addBox.candidate.state === "ready" && !addBox.candidate.canAdd
                                width: parent.width
                                color: "#ff8a80"
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                                text: addBox.candidate.why !== undefined ? addBox.candidate.why : ""
                            }
                            Rectangle {
                                visible: addBox.candidate.state === "ready" && addBox.candidate.canAdd
                                width: parent.width
                                height: 34
                                radius: 6
                                color: "#16181b"
                                border.width: 1
                                border.color: newTitle.activeFocus ? "#4caf50" : "#44ffffff"
                                TextInput {
                                    id: newTitle
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    color: "white"
                                    font.pixelSize: 15
                                    clip: true
                                    maximumLength: 60
                                    Accessible.name: qsTr("Title of the new video")
                                    Text {
                                        visible: newTitle.text === "" && !newTitle.activeFocus
                                        color: "#7d8591"
                                        font.pixelSize: 15
                                        text: addBox.candidate.suggestedTitle !== undefined ? addBox.candidate.suggestedTitle : ""
                                    }
                                }
                            }
                            Row {
                                spacing: 10
                                Pill {
                                    selected: true
                                    text: qsTr("Add to the list")
                                    visible: addBox.candidate.state === "ready"
                                    enabledLook: addBox.candidate.canAdd === true
                                    onClicked: {
                                        saverPage.answered(win.backend.addCandidate(newTitle.text), qsTr("Added. galleond reads the change by itself."))
                                        newTitle.text = ""
                                        pathInput.text = ""
                                    }
                                }
                                Pill {
                                    text: qsTr("Cancel")
                                    onClicked: {
                                        win.backend.clearCandidate()
                                        newTitle.text = ""
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Text {
                width: parent.width
                color: "#7d8591"
                font.pixelSize: 13
                wrapMode: Text.Wrap
                text: qsTr("Cropping a video to the module and converting it come next. Every change is saved at once in %1 (the previous version is kept as .bak) and galleond reads it by itself.").arg(win.backend.file)
            }
        }
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
