// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// Chooses the part of a picture (a video's frame, a photo) that goes to the module. The
// rectangle always has the proportions of what it is for (it cannot be anything else) and stays
// inside the picture; drag it to move it, turn the wheel (or use the buttons) to come closer or
// go further. For the screensaver, the screen and the 12 keys are drawn inside it where they will
// fall, so the person sees what each shows. What the parent puts inside goes under the buttons.
pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: editor

    required property var backend
    required property string preview // the address of the picture shown
    required property int sourceWidth // its size, in pixels
    required property int sourceHeight
    required property int canvasWidth // the size of what the crop is for
    required property int canvasHeight
    // A name for this picture: when it changes, the crop starts again from the middle.
    required property string identity
    // Where the screen and the keys of the module fall in the canvas (optional).
    property var screenRegion: null
    property var keyRegions: []
    // The part kept, in the pixels of the picture: {x, y, width, height}.
    property var crop: ({x: 0, y: 0, width: 1, height: 1})
    property string forIdentity: ""
    property string help: qsTr("Drag the picture to move the frame, turn the wheel to come closer.")
    default property alias extra: extraColumn.data

    readonly property real boxWidth: 400
    readonly property real boxHeight: 480
    // Pixels of the preview for one pixel of the video.
    readonly property real ratio: Math.min(boxWidth / sourceWidth, boxHeight / sourceHeight)
    readonly property real shownWidth: sourceWidth * ratio
    readonly property real shownHeight: sourceHeight * ratio
    // How big the module's picture is drawn inside the crop rectangle.
    readonly property real regionScale: (crop.width * ratio) / canvasWidth

    width: boxWidth + 40 + controls.width
    height: boxHeight

    function centre() {
        crop = backend.cropCenteredFor(sourceWidth, sourceHeight, canvasWidth, canvasHeight)
    }

    function reset() {
        centre()
        forIdentity = identity
    }

    // Another picture than the one being cut: start from the middle.
    onIdentityChanged: {
        if (identity !== forIdentity)
            reset()
    }
    Component.onCompleted: reset()

    function zoom(factor, anchorX, anchorY) {
        crop = backend.cropZoomedFor(crop, factor, anchorX, anchorY, sourceWidth, sourceHeight, canvasWidth, canvasHeight)
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
                source: editor.preview
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
                    visible: editor.screenRegion !== null
                    x: editor.screenRegion !== null ? editor.screenRegion.x * editor.regionScale : 0
                    y: editor.screenRegion !== null ? editor.screenRegion.y * editor.regionScale : 0
                    width: editor.screenRegion !== null ? editor.screenRegion.width * editor.regionScale : 0
                    height: editor.screenRegion !== null ? editor.screenRegion.height * editor.regionScale : 0
                    color: "#22ffffff"
                    border.width: 1
                    border.color: "#cc00e5ff"
                }
                Repeater {
                    model: editor.keyRegions
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
                    editor.crop = editor.backend.cropMovedFor(cropAtPress, Math.round((event.x - pressedAt.x) / editor.ratio),
                                                              Math.round((event.y - pressedAt.y) / editor.ratio), editor.sourceWidth,
                                                              editor.sourceHeight, editor.canvasWidth, editor.canvasHeight)
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
            text: editor.help
        }
        Text {
            color: "#9aa0a6"
            font.pixelSize: 13
            text: qsTr("Keeps %1 × %2 of %3 × %4").arg(editor.crop.width).arg(editor.crop.height).arg(editor.sourceWidth).arg(editor.sourceHeight)
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
                onClicked: editor.centre()
            }
        }
        Column {
            id: extraColumn
            width: parent.width
            spacing: 10
        }
    }
}
