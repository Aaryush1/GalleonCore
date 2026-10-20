// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// What one key does and shows. The person picks the kind of action, fills what it needs, and
// presses Apply: the pages file is written and galleond (when it runs) shows the change at once.
pragma ComponentBehavior: Bound

import QtQuick

Column {
    id: editor

    required property var backend
    required property string page
    required property var key // the entry of backend.keys(page) for the selected key
    // The answer of an edit: the reason when it was refused, else what was done.
    signal answered(string error, string done)

    // What is being edited (the fields of a KeySpec).
    property var spec: ({})
    property string appFilter: ""
    readonly property var kinds: [
        {id: "app", label: qsTr("Application")},
        {id: "media", label: qsTr("Music player")},
        {id: "page", label: qsTr("Open a page")},
        {id: "back", label: qsTr("Back")},
        {id: "key", label: qsTr("Keypad key")}
    ]
    readonly property bool complete: {
        switch (spec.type) {
        case "app": return spec.desktop !== ""
        case "media": return spec.command !== ""
        case "page": return spec.page !== ""
        case "back": return true
        case "key": return spec.keyName !== ""
        case "steam": return true
        default: return false
        }
    }
    readonly property var shownApps: {
        const needle = appFilter.trim().toLowerCase()
        return backend.installedApps().filter(a => needle === "" || a.name.toLowerCase().indexOf(needle) >= 0)
    }

    spacing: 12

    function change(name, value) {
        const copy = Object.assign({}, spec)
        copy[name] = value
        spec = copy
    }

    function load() {
        const k = key
        spec = {
            type: k.empty || k.type === "none" ? "" : k.type,
            desktop: k.desktop || "",
            command: k.command || "",
            step: k.step || 5,
            seconds: k.seconds || 10,
            page: k.pageTarget || "",
            keyName: k.keyName || "",
            appid: k.appid || 0,
            label: k.rawLabel || "",
            icon: k.rawIcon || ""
        }
        appFilter = ""
        Qt.callLater(editor.reveal)
    }

    // The list of applications opens on the one the key has.
    function reveal() {
        const place = shownApps.findIndex(a => a.id === spec.desktop)
        if (place >= 0)
            apps.positionViewAtIndex(place, ListView.Center)
    }

    function kindPicked(id) {
        const copy = Object.assign({}, spec)
        copy.type = id
        if (id === "media" && copy.command === "")
            copy.command = "play-pause"
        spec = copy
    }

    onKeyChanged: load()
    Component.onCompleted: load()

    Text {
        color: "white"
        font.pixelSize: 18
        font.bold: true
        text: qsTr("Key %1").arg(editor.key.index + 1)
    }

    // ---- what the key does
    Text {
        color: "#9aa0a6"
        font.pixelSize: 13
        text: qsTr("It does")
    }
    Flow {
        width: parent.width
        spacing: 6
        Repeater {
            model: editor.kinds
            delegate: Pill {
                required property var modelData
                text: modelData.label
                selected: editor.spec.type === modelData.id
                onClicked: editor.kindPicked(modelData.id)
            }
        }
        Pill {
            visible: editor.spec.type === "steam"
            text: qsTr("Steam game")
            selected: true
        }
    }

    // An application: search the installed ones.
    Column {
        visible: editor.spec.type === "app"
        width: parent.width
        spacing: 6
        Field {
            width: parent.width
            placeholder: qsTr("Search an application…")
            onEdited: text => editor.appFilter = text
            input.onTextChanged: editor.appFilter = input.text
        }
        ListView {
            id: apps
            width: parent.width
            height: 210
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: editor.shownApps
            delegate: Rectangle {
                id: appRow
                required property var modelData
                width: apps.width
                height: 38
                radius: 6
                color: editor.spec.desktop === modelData.id ? "#2e7d32" : (area.containsMouse ? "#2a2d32" : "transparent")
                Image {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26
                    height: 26
                    source: "image://themeicon/" + (appRow.modelData.icon !== "" ? appRow.modelData.icon : "application-x-executable")
                    sourceSize.width: 52
                    sourceSize.height: 52
                    asynchronous: true
                }
                Text {
                    x: 44
                    width: parent.width - 52
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    color: "white"
                    font.pixelSize: 14
                    text: appRow.modelData.name
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: editor.change("desktop", appRow.modelData.id)
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: appRow.modelData.name
            }
        }
        Text {
            visible: editor.spec.desktop !== ""
            width: parent.width
            elide: Text.ElideMiddle
            color: "#7d8591"
            font.pixelSize: 12
            text: editor.spec.desktop
        }
    }

    // The music player.
    Column {
        visible: editor.spec.type === "media"
        width: parent.width
        spacing: 8
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: editor.backend.mediaCommands
                delegate: Pill {
                    required property var modelData
                    text: modelData.label
                    selected: editor.spec.command === modelData.id
                    onClicked: editor.change("command", modelData.id)
                }
            }
        }
        Row {
            visible: editor.spec.command === "volume"
            spacing: 12
            Text {
                height: 32
                verticalAlignment: Text.AlignVCenter
                color: "#c9ced6"
                font.pixelSize: 14
                text: qsTr("Each press changes it by")
            }
            Stepper {
                value: editor.spec.step
                from: -100
                to: 100
                step: 5
                unit: "%"
                onPicked: v => editor.change("step", v === 0 ? (editor.spec.step > 0 ? -5 : 5) : v)
            }
        }
        Row {
            visible: editor.spec.command === "seek"
            spacing: 12
            Text {
                height: 32
                verticalAlignment: Text.AlignVCenter
                color: "#c9ced6"
                font.pixelSize: 14
                text: qsTr("Each press moves by")
            }
            Stepper {
                value: editor.spec.seconds
                from: -3600
                to: 3600
                step: 5
                unit: qsTr("s")
                onPicked: v => editor.change("seconds", v === 0 ? (editor.spec.seconds > 0 ? -5 : 5) : v)
            }
        }
    }

    // Another page.
    Flow {
        visible: editor.spec.type === "page"
        width: parent.width
        spacing: 6
        Repeater {
            model: editor.backend.pageNames.filter(name => name !== editor.page)
            delegate: Pill {
                required property string modelData
                text: modelData
                selected: editor.spec.page === modelData
                onClicked: editor.change("page", modelData)
            }
        }
    }

    // A key of the numeric keypad.
    Flow {
        visible: editor.spec.type === "key"
        width: parent.width
        spacing: 6
        Repeater {
            model: editor.backend.keypadKeys
            delegate: Pill {
                required property string modelData
                text: modelData
                selected: editor.spec.keyName === modelData
                onClicked: editor.change("keyName", modelData)
            }
        }
    }

    Text {
        visible: editor.spec.type === "back"
        width: parent.width
        wrapMode: Text.Wrap
        color: "#9aa0a6"
        font.pixelSize: 13
        text: qsTr("Goes back to the page that was shown before.")
    }
    Text {
        visible: editor.spec.type === "steam"
        width: parent.width
        wrapMode: Text.Wrap
        color: "#9aa0a6"
        font.pixelSize: 13
        text: qsTr("Starts the Steam game number %1. It is kept as it is; give the key another action to replace it.").arg(editor.spec.appid)
    }

    // ---- what it shows
    Row {
        visible: editor.spec.type !== ""
        spacing: 14
        Column {
            spacing: 4
            Text {
                color: "#9aa0a6"
                font.pixelSize: 13
                text: qsTr("Text (empty: the default)")
            }
            Field {
                width: 190
                maximumLength: 24
                placeholder: qsTr("Text")
                value: editor.spec.label
                input.onTextChanged: editor.change("label", input.text)
            }
        }
        Column {
            spacing: 4
            Text {
                color: "#9aa0a6"
                font.pixelSize: 13
                text: qsTr("Icon name (empty: the default)")
            }
            Row {
                spacing: 8
                Field {
                    width: 190
                    maximumLength: 64
                    placeholder: qsTr("icon-name")
                    value: editor.spec.icon
                    input.onTextChanged: editor.change("icon", input.text)
                }
                Image {
                    width: 34
                    height: 34
                    visible: editor.spec.icon !== ""
                    source: editor.spec.icon !== "" ? "image://themeicon/" + editor.spec.icon : ""
                    sourceSize.width: 68
                    sourceSize.height: 68
                }
            }
        }
    }

    // ---- apply
    Row {
        spacing: 10
        Pill {
            selected: true
            text: qsTr("Apply")
            enabledLook: editor.complete
            onClicked: editor.answered(editor.backend.setKey(editor.page, editor.key.index, editor.spec), qsTr("Key saved."))
        }
        Pill {
            text: qsTr("Empty the key")
            danger: true
            enabledLook: !editor.key.empty
            onClicked: editor.answered(editor.backend.clearKey(editor.page, editor.key.index), qsTr("Key emptied."))
        }
        Pill {
            text: qsTr("Undo")
            onClicked: editor.load()
        }
    }
}
