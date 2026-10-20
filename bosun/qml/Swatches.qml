// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// A colour among a few, or written as #rrggbb.
pragma ComponentBehavior: Bound

import QtQuick

Column {
    id: swatches
    property string color: "#ffd600"
    signal picked(string color)
    spacing: 8

    readonly property var palette: ["#ffd600", "#ff9800", "#e53935", "#ec407a", "#ab47bc", "#7e57c2", "#3949ab", "#1e88e5",
                                    "#00acc1", "#26a69a", "#43a047", "#9ccc65", "#8d6e63", "#9aa0a6", "#ffffff"]

    Flow {
        width: 330
        spacing: 6
        Repeater {
            model: swatches.palette
            delegate: Rectangle {
                id: chip
                required property string modelData
                width: 30
                height: 30
                radius: 6
                color: chip.modelData
                border.width: swatches.color.toLowerCase() === chip.modelData ? 3 : 1
                border.color: swatches.color.toLowerCase() === chip.modelData ? "white" : "#66ffffff"
                MouseArea {
                    anchors.fill: parent
                    onClicked: swatches.picked(chip.modelData)
                }
                Accessible.role: Accessible.Button
                Accessible.name: chip.modelData
            }
        }
    }
    Row {
        spacing: 10
        Field {
            width: 110
            maximumLength: 7
            placeholder: "#rrggbb"
            value: swatches.color
            onEdited: text => {
                if (/^#[0-9a-fA-F]{6}$/.test(text))
                    swatches.picked(text.toLowerCase())
            }
        }
        Rectangle {
            width: 34
            height: 34
            radius: 6
            color: swatches.color
            border.width: 1
            border.color: "#66ffffff"
        }
    }
}
