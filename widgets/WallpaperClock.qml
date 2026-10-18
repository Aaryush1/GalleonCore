// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared widget: a wallpaper with the time over it. Rendered off-screen by
// galleond for the Galleon's screen, and usable as is in the preview window or
// a plasmoid. Keep it free of PlasmaCore/Kirigami imports.
//
// The wallpapers keep their left third calm and dark on purpose (see
// public/wallpaper/README.md): the clock sits there.

import QtQuick

Rectangle {
    id: root

    // The image to show (a file: URL). Empty: plain black.
    property url wallpaper
    // The time to show. With `live` it follows the system clock by itself;
    // galleond sets it explicitly and turns `live` off, so a frame is
    // reproducible.
    property date now: new Date()
    property bool live: true
    property color textColor: "white"
    property bool showSeconds: false
    // True once the wallpaper is decoded (or when there is none): the renderer
    // waits for it before taking a frame.
    readonly property bool ready: root.wallpaper.toString() === "" || image.status === Image.Ready

    implicitWidth: 720
    implicitHeight: 384
    color: "black"
    clip: true

    Image {
        id: image
        anchors.fill: parent
        source: root.wallpaper
        fillMode: Image.PreserveAspectCrop
        asynchronous: false
        cache: false
        smooth: true
        // Decode at twice the target at most: the originals are large.
        sourceSize: Qt.size(root.width * 2, root.height * 2)
    }

    // Darkens the left side so the text stays readable on any image.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#c8000000" }
            GradientStop { position: 0.55; color: "#66000000" }
            GradientStop { position: 1.0; color: "#00000000" }
        }
    }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: root.height * 0.08
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.height * 0.01

        Text {
            color: root.textColor
            font.pixelSize: root.height * 0.30
            font.bold: true
            text: Qt.formatTime(root.now, root.showSeconds ? "hh:mm:ss" : "hh:mm")
        }

        Text {
            color: root.textColor
            opacity: 0.8
            font.pixelSize: root.height * 0.075
            text: Qt.formatDate(root.now, "dddd d MMMM")
        }
    }

    Timer {
        interval: 1000
        running: root.live
        repeat: true
        onTriggered: root.now = new Date()
    }
}
