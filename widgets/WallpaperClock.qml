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
    // The colour of the line along the bottom edge (transparent: none).
    property color barColor: "transparent"
    // The theme shown as a line along the top (empty name: none).
    property string themeName
    property color themeColor: "white"
    // True while the theme is being chosen with a dial: the name is shown large.
    property bool themeChoosing: false
    // Language, 12/24 hour clock and date order of the text. The system's by
    // default; QML's own Qt.formatDate()/formatTime() ignore it and always
    // give English, so the formats are taken from this locale instead.
    property var locale: Qt.locale()
    // The locale's short time format ("HH:mm", "h:mm AP"...), with seconds if asked.
    readonly property string timeFormat: showSeconds
        ? locale.timeFormat(Locale.ShortFormat).replace("mm", "mm:ss")
        : locale.timeFormat(Locale.ShortFormat)
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
            text: root.locale.toString(root.now, root.timeFormat)
        }

        Text {
            color: root.textColor
            opacity: 0.8
            font.pixelSize: root.height * 0.075
            text: root.locale.toString(root.now, root.locale.dateFormat(Locale.LongFormat))
        }
    }

    Timer {
        interval: 1000
        running: root.live
        repeat: true
        onTriggered: root.now = new Date()
    }
    // The current theme: a line along the top edge in its colour, with its
    // name at the right. Nothing without a theme.
    Rectangle {
        visible: root.themeName !== ""
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 6 * root.height / 384
        color: root.themeColor
    }
    Text {
        visible: root.themeName !== ""
        anchors.top: parent.top
        anchors.topMargin: 10 * root.height / 384
        anchors.right: parent.right
        anchors.rightMargin: 16 * root.height / 384
        color: root.themeColor
        font.pixelSize: 15 * root.height / 384
        font.bold: true
        text: root.themeName
    }
    // A thin line along the bottom edge: which control the right dial has
    // (white: the player, magenta: choosing the wallpaper). None when transparent.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 4 * root.height / 384
        color: root.barColor
    }
    // While the theme is being chosen: its name in a band, with arrows.
    Rectangle {
        visible: root.themeChoosing && root.themeName !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18 * root.height / 384
        width: Math.min(root.width - 32 * root.height / 384, choosingText.implicitWidth + 64 * root.height / 384)
        height: 64 * root.height / 384
        radius: 14 * root.height / 384
        color: "#d9000000"
        border.width: 3 * root.height / 384
        border.color: root.themeColor
        Text {
            id: choosingText
            anchors.centerIn: parent
            color: root.themeColor
            font.pixelSize: 34 * root.height / 384
            font.bold: true
            text: "\u25C0  " + root.themeName + "  \u25B6"
        }
    }
}
