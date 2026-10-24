// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
Rectangle {
    id: root
    property string label
    property real value: -1
    property string temperature: "—"
    property color accent: "#5de1d3"
    readonly property bool hasValue: isFinite(value) && value >= 0 && value <= 100
    implicitWidth: 208
    implicitHeight: 226
    color: "#dc0b141e"
    radius: width * 0.07
    border.color: "#36495a"
    Text {
        x: root.width * 0.08; y: root.height * 0.045
        text: root.label; color: "#e1eaf2"
        font.pixelSize: root.height * 0.085; font.bold: true
    }
    Item {
        id: dial
        x: 0; y: root.height * 0.15
        width: root.width; height: root.height * 0.72
        readonly property real cx: width / 2
        readonly property real cy: height * 0.53
        readonly property real r: Math.min(width * 0.40, height * 0.49)
        Repeater {
            model: 41
            delegate: Rectangle {
                id: segment
                required property int index
                x: dial.cx + dial.r - width / 2; y: dial.cy - height / 2
                width: dial.r * 0.13; height: dial.r * 0.055
                radius: height / 2
                color: root.hasValue && root.value > 0 && index * 2.5 <= root.value
                    ? (root.value >= 90 ? "#ff7777" : root.value >= 70 ? "#ffcd75" : root.accent) : "#2b3a49"
                transform: Rotation { origin.x: -dial.r + segment.width / 2; origin.y: segment.height / 2; angle: 135 + segment.index * 6.75 }
            }
        }
        Repeater {
            model: 21
            delegate: Rectangle {
                id: tick
                required property int index
                x: dial.cx + dial.r * 0.77 - width / 2; y: dial.cy - height / 2
                width: dial.r * (index % 5 === 0 ? 0.17 : 0.09)
                height: index % 5 === 0 ? 2 : 1
                color: index >= 18 ? "#ff7777" : "#9bacbb"
                transform: Rotation { origin.x: -dial.r * 0.77 + tick.width / 2; origin.y: tick.height / 2; angle: 135 + tick.index * 13.5 }
            }
        }
        Rectangle {
            visible: root.hasValue
            x: dial.cx + dial.r * 0.64; y: dial.cy - height / 2
            width: dial.r * 0.42; height: 3; radius: 1.5
            color: "white"
            transform: Rotation { origin.x: -dial.r * 0.64; origin.y: 1.5; angle: 135 + Math.max(0, Math.min(100, root.value)) * 2.7 }
        }
        Text {
            x: dial.cx - dial.r * 0.90; y: dial.cy + dial.r * 0.78
            width: dial.r * 0.36; horizontalAlignment: Text.AlignHCenter
            text: "0"; color: "#a8bac9"; font.pixelSize: dial.r * 0.16
        }
        Text {
            x: dial.cx + dial.r * 0.54; y: dial.cy + dial.r * 0.78
            width: dial.r * 0.36; horizontalAlignment: Text.AlignHCenter
            text: "100"; color: "#a8bac9"; font.pixelSize: dial.r * 0.16
        }
    }
    Text {
        x: 0; y: root.height * 0.40
        width: root.width; height: root.height * 0.23
        horizontalAlignment: Text.AlignHCenter
        text: root.hasValue ? Number(root.value).toLocaleString(Qt.locale(), "f", 0) + " %" : "—"
        color: "white"; font.bold: true; font.pixelSize: root.height * 0.17
        fontSizeMode: Text.Fit; minimumPixelSize: 12
    }
    Text {
        x: root.width * 0.04; y: root.height * 0.85
        width: root.width * 0.92; height: root.height * 0.13
        text: root.temperature
        horizontalAlignment: Text.AlignHCenter
        color: "#d8e7f3"; font.pixelSize: root.height * 0.11
        font.bold: true; fontSizeMode: Text.Fit; minimumPixelSize: 10
    }
}
