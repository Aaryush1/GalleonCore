// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// A small button for the controls of the settings.
import QtQuick

Rectangle {
    id: pill
    property alias text: label.text
    property bool danger: false
    property bool selected: false // the value now in force, among several
    property bool enabledLook: true
    signal clicked
    width: Math.max(34, label.implicitWidth + 24)
    height: 32
    radius: 6
    color: !pill.enabledLook ? "#2a2c30" : pill.selected ? "#2e7d32" : (area.containsMouse ? (pill.danger ? "#7a3b3b" : "#3a3f47") : (pill.danger ? "#5b3a3a" : "#2e3238"))
    Text {
        id: label
        anchors.centerIn: parent
        color: pill.enabledLook ? "white" : "#6d737c"
        font.pixelSize: 14
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        enabled: pill.enabledLook
        onClicked: pill.clicked()
    }
    Accessible.role: Accessible.Button
    Accessible.name: label.text
}
