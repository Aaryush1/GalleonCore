// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// The tab of a theme: its name, its colour, its place among the tabs, and its removal.
pragma ComponentBehavior: Bound

import QtQuick

Column {
    id: editor

    required property var backend
    required property int index // the theme's place in the list
    required property var theme // {name, page, color}
    property bool confirming: false
    signal answered(string error, string done)
    // The tab moved or went: the window follows it.
    signal moved(int to)
    signal removed

    spacing: 10

    Timer {
        id: forget
        interval: 4000
        onTriggered: editor.confirming = false
    }

    Text {
        color: "white"
        font.pixelSize: 16
        font.bold: true
        text: qsTr("This tab")
    }
    Field {
        width: 260
        maximumLength: 24
        bold: true
        placeholder: qsTr("Name of the tab")
        value: editor.theme.name
        onEdited: text => editor.answered(editor.backend.setTheme(editor.index, text, editor.theme.color), qsTr("Name saved."))
    }
    Swatches {
        color: editor.theme.color
        onPicked: c => editor.answered(editor.backend.setTheme(editor.index, editor.theme.name, c), qsTr("Colour saved."))
    }
    Row {
        spacing: 8
        Pill {
            text: qsTr("← Move left")
            enabledLook: editor.index > 0
            onClicked: {
                const error = editor.backend.moveTheme(editor.index, -1)
                editor.answered(error, qsTr("Moved."))
                if (error === "")
                    editor.moved(editor.index - 1)
            }
        }
        Pill {
            text: qsTr("Move right →")
            enabledLook: editor.index < editor.backend.themes.length - 1
            onClicked: {
                const error = editor.backend.moveTheme(editor.index, 1)
                editor.answered(error, qsTr("Moved."))
                if (error === "")
                    editor.moved(editor.index + 1)
            }
        }
        Pill {
            danger: true
            text: editor.confirming ? qsTr("Click again to remove") : qsTr("Remove this tab")
            onClicked: {
                if (!editor.confirming) {
                    editor.confirming = true
                    forget.restart()
                    return
                }
                editor.confirming = false
                const error = editor.backend.removeTheme(editor.index)
                editor.answered(error, qsTr("Tab removed."))
                if (error === "")
                    editor.removed()
            }
        }
    }
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        color: "#7d8591"
        font.pixelSize: 12
        text: qsTr("Page “%1”. Removing a tab removes its page too, unless a key still leads to it. The tab that opens the home page cannot be removed.").arg(editor.theme.page)
    }
}
