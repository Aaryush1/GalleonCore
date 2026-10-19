// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared widget: the track being played, with its cover and its timeline.
// Rendered off-screen by galleond for the Galleon's screen. Keep it free of
// PlasmaCore/Kirigami imports.
//
// Sizes follow the height, the layout being drawn for 720 x 384.

import QtQuick

Rectangle {
    id: root

    property string title
    property string artist
    property real positionSeconds: 0
    // 0 when unknown (a stream): the bar is then not drawn.
    property real durationSeconds: 0
    property bool playing: true
    // The id of a cover held by the image provider "galleon"; empty: none.
    property string coverId
    // The picture is an application icon, not a cover: shown whole, with room around it.
    property bool coverIsIcon: false
    // The colour of the line along the bottom edge (transparent: none).
    property color barColor: "transparent"
    // The theme shown as a line along the top (empty name: none).
    property string themeName
    property color themeColor: "white"
    property date now: new Date()
    property var locale: Qt.locale()
    // True once the cover is decoded (or when there is none).
    readonly property bool ready: root.coverId === "" || cover.status === Image.Ready

    // Whole minutes and seconds: "3:07", or "1:02:03" past an hour.
    function clock(seconds) {
        const total = Math.max(0, Math.floor(seconds))
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        const two = (n) => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + two(m) + ":" + two(s) : m + ":" + two(s)
    }
    readonly property real fraction: durationSeconds > 0 ? Math.min(1, Math.max(0, positionSeconds / durationSeconds)) : 0

    readonly property real unit: height / 384
    readonly property real margin: 24 * unit
    readonly property real coverSize: height - 2 * margin
    readonly property real textLeft: margin + coverSize + 28 * unit
    readonly property real textWidth: width - textLeft - margin

    implicitWidth: 720
    implicitHeight: 384
    color: "#14171c"
    clip: true

    // The cover, or a placeholder when there is none.
    Rectangle {
        x: root.margin
        y: root.margin
        width: root.coverSize
        height: root.coverSize
        radius: 14 * root.unit
        color: "#232830"
        clip: true

        Text {
            anchors.centerIn: parent
            visible: root.coverId === ""
            color: "#59616d"
            font.pixelSize: parent.height * 0.5
            text: "♪"
        }
        Image {
            id: cover
            anchors.fill: parent
            anchors.margins: root.coverIsIcon ? parent.width * 0.16 : 0
            visible: root.coverId !== ""
            source: root.coverId !== "" ? "image://galleon/" + root.coverId : ""
            fillMode: root.coverIsIcon ? Image.PreserveAspectFit : Image.PreserveAspectCrop
            asynchronous: false
            cache: false
            smooth: true
        }
    }

    Text {
        x: root.textLeft
        y: root.margin
        width: root.textWidth
        color: "#7d8591"
        font.pixelSize: 20 * root.unit
        elide: Text.ElideRight
        text: root.locale.toString(root.now, root.locale.timeFormat(Locale.ShortFormat))
    }

    Text {
        id: titleText
        x: root.textLeft
        y: root.margin + 36 * root.unit
        width: root.textWidth
        height: 132 * root.unit
        color: "white"
        font.pixelSize: 34 * root.unit
        font.bold: true
        wrapMode: Text.Wrap
        maximumLineCount: 3
        elide: Text.ElideRight
        text: root.title
    }

    Text {
        x: root.textLeft
        y: root.margin + 176 * root.unit
        width: root.textWidth
        color: "#b3bac4"
        font.pixelSize: 24 * root.unit
        elide: Text.ElideRight
        text: root.artist
    }

    // The timeline.
    Rectangle {
        id: track
        visible: root.durationSeconds > 0
        x: root.textLeft
        y: root.height - root.margin - 46 * root.unit
        width: root.textWidth
        height: 6 * root.unit
        radius: height / 2
        color: "#333a44"

        Rectangle {
            width: parent.width * root.fraction
            height: parent.height
            radius: parent.radius
            color: root.playing ? "#4caf50" : "#8a929d"
        }
    }

    Text {
        x: root.textLeft
        y: root.height - root.margin - 30 * root.unit
        color: "#9aa2ad"
        font.pixelSize: 18 * root.unit
        text: root.clock(root.positionSeconds)
    }

    Text {
        visible: root.durationSeconds > 0
        x: root.textLeft + root.textWidth - width
        y: root.height - root.margin - 30 * root.unit
        color: "#9aa2ad"
        font.pixelSize: 18 * root.unit
        text: root.clock(root.durationSeconds)
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
}
