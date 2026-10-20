// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// Chooses the part of a video that goes to the module. The rectangle always has the proportions
// of the module's picture (it cannot be anything else) and stays inside the video; drag it to
// move it, turn the wheel (or use the buttons) to come closer or go further. Inside it, the
// screen and the 12 keys are drawn where they will fall, so the person sees what each shows.
pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: editor

    required property var backend
    required property var candidate // the entry of backend.candidate for a video that can be cut
    // The part kept, in the pixels of the video: {x, y, width, height}.
    property var crop: ({x: 0, y: 0, width: 1, height: 1})
    property string forPath: ""
    // The title the video will have in the list.
    property string title: ""
    signal answered(string error, string done)

    readonly property real boxWidth: 400
    readonly property real boxHeight: 480
    // Pixels of the preview for one pixel of the video.
    readonly property real ratio: Math.min(boxWidth / candidate.width, boxHeight / candidate.height)
    readonly property real shownWidth: candidate.width * ratio
    readonly property real shownHeight: candidate.height * ratio
    // How big the module's picture is drawn inside the crop rectangle.
    readonly property real regionScale: (crop.width * ratio) / candidate.canvasWidth

    width: boxWidth + 40 + controls.width
    height: boxHeight

    function reset() {
        crop = candidate.crop
        forPath = candidate.path
    }

    // A video that is not the one being cut: start from the middle.
    onCandidateChanged: {
        if (candidate.path !== forPath)
            reset()
    }
    Component.onCompleted: reset()

    function zoom(factor, anchorX, anchorY) {
        crop = backend.cropZoomed(crop, factor, anchorX, anchorY)
    }

    // ---- the video and the rectangle
    Item {
        id: view
        width: editor.boxWidth
        height: editor.boxHeight
        clip: true

        Rectangle {
            anchors.fill: parent
            color: "#0f1113"
        }
        Item {
            id: picture
            anchors.centerIn: parent
            width: editor.shownWidth
            height: editor.shownHeight

            Image {
                anchors.fill: parent
                source: editor.candidate.thumbnail
                fillMode: Image.Stretch
                asynchronous: true
                cache: false
            }
            // What is cut off, darkened.
            Rectangle { x: 0; y: 0; width: parent.width; height: cropBox.y; color: "#bb000000" }
            Rectangle { x: 0; y: cropBox.y + cropBox.height; width: parent.width; height: parent.height - (cropBox.y + cropBox.height); color: "#bb000000" }
            Rectangle { x: 0; y: cropBox.y; width: cropBox.x; height: cropBox.height; color: "#bb000000" }
            Rectangle { x: cropBox.x + cropBox.width; y: cropBox.y; width: parent.width - (cropBox.x + cropBox.width); height: cropBox.height; color: "#bb000000" }

            // The part that is kept, with the screen and the keys of the module inside.
            Item {
                id: cropBox
                x: editor.crop.x * editor.ratio
                y: editor.crop.y * editor.ratio
                width: editor.crop.width * editor.ratio
                height: editor.crop.height * editor.ratio

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: 2
                    border.color: "#ffd600"
                }
                Rectangle {
                    x: editor.candidate.screen.x * editor.regionScale
                    y: editor.candidate.screen.y * editor.regionScale
                    width: editor.candidate.screen.width * editor.regionScale
                    height: editor.candidate.screen.height * editor.regionScale
                    color: "#22ffffff"
                    border.width: 1
                    border.color: "#cc00e5ff"
                }
                Repeater {
                    model: editor.candidate.keyRegions
                    delegate: Rectangle {
                        required property var modelData
                        x: modelData.x * editor.regionScale
                        y: modelData.y * editor.regionScale
                        width: modelData.width * editor.regionScale
                        height: modelData.height * editor.regionScale
                        radius: 3
                        color: "transparent"
                        border.width: 1
                        border.color: "#aaffffff"
                    }
                }
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                property point pressedAt
                property var cropAtPress
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                onPressed: event => {
                    pressedAt = Qt.point(event.x, event.y)
                    cropAtPress = editor.crop
                }
                onPositionChanged: event => {
                    if (!pressed)
                        return
                    // Counted from where the mouse went down, so that it does not drift.
                    editor.crop = editor.backend.cropMoved(cropAtPress, Math.round((event.x - pressedAt.x) / editor.ratio),
                                                           Math.round((event.y - pressedAt.y) / editor.ratio))
                }
                onWheel: event => editor.zoom(event.angleDelta.y > 0 ? 1.1 : 1 / 1.1, event.x / editor.ratio, event.y / editor.ratio)
            }
        }
    }

    // ---- the controls
    Column {
        id: controls
        x: editor.boxWidth + 40
        width: 230
        spacing: 10

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            color: "#c9ced6"
            font.pixelSize: 13
            text: qsTr("Drag the picture to move the frame, turn the wheel to come closer. The module shows what is inside the yellow frame: the cyan part is its screen, the squares are its 12 keys.")
        }
        Text {
            color: "#9aa0a6"
            font.pixelSize: 13
            text: qsTr("Keeps %1 × %2 of %3 × %4").arg(editor.crop.width).arg(editor.crop.height).arg(editor.candidate.width).arg(editor.candidate.height)
        }
        Row {
            spacing: 6
            Pill {
                text: "−"
                onClicked: editor.zoom(1 / 1.25, editor.crop.x + editor.crop.width / 2, editor.crop.y + editor.crop.height / 2)
            }
            Pill {
                text: "+"
                onClicked: editor.zoom(1.25, editor.crop.x + editor.crop.width / 2, editor.crop.y + editor.crop.height / 2)
            }
            Pill {
                text: qsTr("Centre")
                onClicked: editor.crop = editor.backend.cropCentered()
            }
        }
        Pill {
            selected: true
            text: qsTr("Cut and add to the list")
            enabledLook: editor.backend.conversion.state !== "running"
            onClicked: editor.answered(editor.backend.startConversion(editor.crop.x, editor.crop.y, editor.crop.width, editor.crop.height, editor.title),
                                       qsTr("Cutting the video…"))
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            color: "#7d8591"
            font.pixelSize: 12
            text: qsTr("FFmpeg writes a new video of the module's size in your screensavers folder. Your original file is not touched.")
        }
    }
}
