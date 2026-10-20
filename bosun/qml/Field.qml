// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// A line of text to edit. `edited` is sent when the person is done (Enter, or the field loses
// the focus) and the text is not what `value` was.
import QtQuick

Rectangle {
    id: field
    property string value: ""
    property string placeholder: ""
    property int maximumLength: 60
    property bool bold: false
    property alias input: input
    signal edited(string text)
    width: 240
    height: 34
    radius: 6
    color: "#16181b"
    border.width: 1
    border.color: input.activeFocus ? "#4caf50" : "#44ffffff"

    TextInput {
        id: input
        anchors.fill: parent
        anchors.margins: 8
        color: "white"
        font.pixelSize: 15
        font.bold: field.bold
        clip: true
        selectByMouse: true
        maximumLength: field.maximumLength
        text: field.value
        onEditingFinished: {
            if (text !== field.value)
                field.edited(text)
        }
        Accessible.name: field.placeholder
        Text {
            visible: input.text === "" && !input.activeFocus
            color: "#7d8591"
            font.pixelSize: 15
            text: field.placeholder
        }
    }
}
