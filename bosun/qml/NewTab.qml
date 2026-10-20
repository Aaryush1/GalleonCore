// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// A new tab: a name and a colour. It starts with 12 empty keys.
import QtQuick

Column {
    id: form

    required property var backend
    property string name: ""
    property string color: "#00acc1"
    property bool withPlus: false
    signal answered(string error, string done)
    // The tab was made: the window shows it.
    signal created

    spacing: 12

    Text {
        color: "white"
        font.pixelSize: 18
        font.bold: true
        text: qsTr("A new tab")
    }
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        color: "#9aa0a6"
        font.pixelSize: 14
        text: qsTr("A tab is a theme: its own page of 12 keys, and its colour on the module's screen, rings and keys. Up to 8.")
    }
    Field {
        width: 260
        maximumLength: 24
        placeholder: qsTr("Name of the tab")
        value: form.name
        input.onTextChanged: form.name = input.text
    }
    Swatches {
        color: form.color
        onPicked: c => form.color = c
    }
    Row {
        spacing: 10
        Pill {
            text: form.withPlus ? qsTr("✓ With a “plus” page") : qsTr("With a “plus” page")
            selected: form.withPlus
            onClicked: form.withPlus = !form.withPlus
        }
        Text {
            height: 32
            verticalAlignment: Text.AlignVCenter
            color: "#7d8591"
            font.pixelSize: 12
            text: qsTr("12 more keys: the last key of the tab leads to a second page.")
        }
    }
    Pill {
        selected: true
        text: qsTr("Create the tab")
        enabledLook: form.name.trim() !== ""
        onClicked: {
            let error = form.backend.addTheme(form.name, form.color)
            if (error === "" && form.withPlus)
                error = form.backend.addSubPage(form.backend.themes[form.backend.themes.length - 1].page)
            form.answered(error, qsTr("Tab created."))
            if (error === "") {
                form.name = ""
                form.created()
            }
        }
    }
}
