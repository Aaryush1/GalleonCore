// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared widget: rendered off-screen by galleond and shown by the plasmoid.
// Keep it free of PlasmaCore/Kirigami imports.

import QtQuick

Rectangle {
    id: root

    property color textColor: "white"
    property bool showSeconds: false

    implicitWidth: 320
    implicitHeight: 180
    color: "black"

    Column {
        anchors.centerIn: parent
        spacing: 4

        Text {
            id: time
            anchors.horizontalCenter: parent.horizontalCenter
            color: root.textColor
            font.pixelSize: root.height * 0.35
            font.bold: true
        }

        Text {
            id: date
            anchors.horizontalCenter: parent.horizontalCenter
            color: root.textColor
            opacity: 0.7
            font.pixelSize: root.height * 0.12
        }
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            const now = new Date()
            time.text = Qt.formatTime(now, root.showSeconds ? "hh:mm:ss" : "hh:mm")
            date.text = Qt.formatDate(now, "dddd d MMMM")
        }
    }
}
