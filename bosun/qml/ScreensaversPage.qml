// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// The screensavers: the settings, the videos with what is known of each, and adding a video.
pragma ComponentBehavior: Bound

import QtQuick

Flickable {
    id: saverPage
    required property var backend
    readonly property var saver: backend.screensaver
    property string message: ""
    property bool messageIsError: false
    // Opens the crop window as soon as there is a video to cut (for a picture of it).
    property bool cropAtStart: false
    onCropAtStartChanged: addBox.cropping = cropAtStart
    contentWidth: width
    contentHeight: saverColumn.height
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    // What an edit answered: nothing when it was done, else why not.
    function answered(error, done) {
        message = error === "" ? done : error
        messageIsError = error !== ""
    }

    Column {
        id: saverColumn
        width: saverPage.width
        spacing: 14

        Text {
            color: "white"
            font.pixelSize: 18
            font.bold: true
            text: qsTr("Screensavers")
        }
        Text {
            width: parent.width
            color: saverPage.saver.enabled ? "#9be7a0" : "#ffb74d"
            font.pixelSize: 14
            wrapMode: Text.Wrap
            text: {
                if (!saverPage.saver.hasGeometry)
                    return qsTr("Off: the measures of the module (screen, keys, gaps) are missing from the pages file, so a video cannot be spread over it.")
                if (saverPage.saver.videos.length === 0)
                    return qsTr("Off: no video.")
                return qsTr("On.")
            }
        }

        // The settings.
        Grid {
            columns: 2
            columnSpacing: 20
            rowSpacing: 12
            verticalItemAlignment: Grid.AlignVCenter

            Text {
                color: "#c9ced6"
                font.pixelSize: 14
                text: qsTr("Starts after")
            }
            Row {
                spacing: 14
                Stepper {
                    value: saverPage.saver.idleMinutes
                    from: 1
                    to: 720
                    unit: qsTr("min")
                    onPicked: v => saverPage.answered(saverPage.backend.setIdleMinutes(v), qsTr("Saved."))
                }
                Repeater {
                    model: [1, 2, 5, 10, 30, 60]
                    delegate: Pill {
                        required property int modelData
                        text: modelData
                        selected: saverPage.saver.idleMinutes === modelData
                        onClicked: saverPage.answered(saverPage.backend.setIdleMinutes(modelData), qsTr("Saved."))
                    }
                }
            }

            Text {
                color: "#c9ced6"
                font.pixelSize: 14
                text: qsTr("Images per second")
            }
            Row {
                spacing: 14
                Stepper {
                    value: saverPage.saver.fps
                    from: 1
                    to: 20
                    onPicked: v => saverPage.answered(saverPage.backend.setFps(v), qsTr("Saved."))
                }
                Text {
                    height: 32
                    verticalAlignment: Text.AlignVCenter
                    color: "#7d8591"
                    font.pixelSize: 12
                    text: qsTr("The module shows about 20 at most, screen and keys together.")
                }
            }

            Text {
                color: "#c9ced6"
                font.pixelSize: 14
                text: qsTr("While a track plays")
            }
            Row {
                spacing: 8
                Pill {
                    text: qsTr("Never")
                    selected: !saverPage.saver.whilePlaying
                    onClicked: saverPage.answered(saverPage.backend.setWhilePlaying(false), qsTr("Saved."))
                }
                Pill {
                    text: qsTr("Allowed")
                    selected: saverPage.saver.whilePlaying
                    onClicked: saverPage.answered(saverPage.backend.setWhilePlaying(true), qsTr("Saved."))
                }
            }
        }

        Text {
            width: parent.width
            visible: saverPage.message !== ""
            color: saverPage.messageIsError ? "#ff8a80" : "#9be7a0"
            font.pixelSize: 13
            wrapMode: Text.Wrap
            text: saverPage.message
        }
        Text {
            visible: saverPage.saver.probeMissing
            width: parent.width
            color: "#ffb74d"
            font.pixelSize: 13
            wrapMode: Text.Wrap
            text: qsTr("ffprobe was not found (it comes with FFmpeg): the size, rate and length of the videos cannot be shown.")
        }
        Text {
            visible: saverPage.saver.canvas !== ""
            width: parent.width
            color: "#9aa0a6"
            font.pixelSize: 13
            wrapMode: Text.Wrap
            text: qsTr("A video is cut to %1 pixels: the screen and the 12 keys seen as one picture.").arg(saverPage.saver.canvas)
        }

        Repeater {
            model: saverPage.saver.videos
            delegate: Rectangle {
                id: videoRow
                required property var modelData
                property bool confirming: false
                width: saverColumn.width
                height: Math.max(150, info.height + 28)
                radius: 10
                color: "#23252a"

                Timer {
                    id: forget
                    interval: 4000
                    onTriggered: videoRow.confirming = false
                }

                Rectangle {
                    id: pictureFrame
                    x: 14
                    y: 14
                    width: 84
                    height: 122
                    radius: 6
                    color: "#15171a"
                    Image {
                        anchors.fill: parent
                        visible: videoRow.modelData.thumbnail !== ""
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        source: videoRow.modelData.thumbnail
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: videoRow.modelData.thumbnail === ""
                        color: "#5f6670"
                        font.pixelSize: 12
                        text: videoRow.modelData.found ? qsTr("…") : qsTr("none")
                    }
                }
                Column {
                    id: info
                    x: 114
                    y: 14
                    width: parent.width - 130 - removeButton.width
                    spacing: 6

                    // The title shown while the screensaver runs; empty: the name of the file.
                    Rectangle {
                        width: parent.width
                        height: 34
                        radius: 6
                        color: "#16181b"
                        border.width: 1
                        border.color: title.activeFocus ? "#4caf50" : "#44ffffff"
                        TextInput {
                            id: title
                            anchors.fill: parent
                            anchors.margins: 8
                            color: "white"
                            font.pixelSize: 15
                            font.bold: true
                            clip: true
                            maximumLength: 60
                            text: videoRow.modelData.title
                            onEditingFinished: {
                                if (text.trim() !== videoRow.modelData.title)
                                    saverPage.answered(saverPage.backend.setVideoTitle(videoRow.modelData.index, text), qsTr("Title saved."))
                            }
                            Accessible.name: qsTr("Title of the video")
                            Text {
                                visible: title.text === "" && !title.activeFocus
                                color: "#7d8591"
                                font.pixelSize: 15
                                text: videoRow.modelData.name
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideMiddle
                        color: "#7d8591"
                        font.pixelSize: 12
                        text: videoRow.modelData.file
                    }
                    Text {
                        visible: videoRow.modelData.details !== ""
                        width: parent.width
                        color: "#c9ced6"
                        font.pixelSize: 14
                        text: videoRow.modelData.details
                    }
                    Repeater {
                        model: videoRow.modelData.notes
                        delegate: Text {
                            required property string modelData
                            width: info.width
                            color: "#ffb74d"
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                            text: "• " + modelData
                        }
                    }
                }
                Pill {
                    id: removeButton
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    y: 14
                    danger: true
                    text: videoRow.confirming ? qsTr("Click again to remove") : qsTr("Remove")
                    onClicked: {
                        if (!videoRow.confirming) {
                            videoRow.confirming = true
                            forget.restart()
                            return
                        }
                        saverPage.answered(saverPage.backend.removeVideo(videoRow.modelData.index),
                                           saverPage.saver.videos.length <= 1 ? qsTr("Removed: no video is left, the screensaver is off.") : qsTr("Removed from the list (the file is kept)."))
                    }
                }
            }
        }

        // ---- adding a video: choose it (drop it or give its path), look at it, then add it
        Rectangle {
            id: addBox
            readonly property var candidate: saverPage.backend.candidate
            readonly property bool chosen: candidate.path !== undefined || candidate.name !== undefined
            readonly property string chosenPath: candidate.path !== undefined ? candidate.path : ""
            property bool cropping: false
            // Another video, or none: the crop window closes.
            onChosenPathChanged: cropping = false
            width: parent.width
            height: addColumn.height + 28
            radius: 10
            color: drop.containsDrag ? "#1f3a2a" : "#1f2125"
            border.width: 1
            border.color: drop.containsDrag ? "#4caf50" : "#33ffffff"

            DropArea {
                id: drop
                anchors.fill: parent
                keys: ["text/uri-list"]
                onDropped: event => {
                    if (event.hasUrls && event.urls.length > 0) {
                        saverPage.backend.inspect(event.urls[0].toString())
                        saverPage.message = ""
                    }
                }
            }

            Column {
                id: addColumn
                x: 14
                y: 14
                width: parent.width - 28
                spacing: 10

                Text {
                    color: "white"
                    font.pixelSize: 16
                    font.bold: true
                    text: qsTr("Add a video")
                }
                Text {
                    width: parent.width
                    color: "#9aa0a6"
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    text: qsTr("Drop a video file here, or write its path. It is only looked at until you add it: as it is, where it is, or cut to the module's picture (that makes a new video in your screensavers folder).")
                }
                Row {
                    spacing: 10
                    Rectangle {
                        width: addColumn.width - checkButton.width - 10
                        height: 34
                        radius: 6
                        color: "#16181b"
                        border.width: 1
                        border.color: pathInput.activeFocus ? "#4caf50" : "#44ffffff"
                        TextInput {
                            id: pathInput
                            anchors.fill: parent
                            anchors.margins: 8
                            color: "white"
                            font.pixelSize: 14
                            clip: true
                            selectByMouse: true
                            onAccepted: checkButton.clicked()
                            Accessible.name: qsTr("Path of a video file")
                            Text {
                                visible: pathInput.text === "" && !pathInput.activeFocus
                                color: "#7d8591"
                                font.pixelSize: 14
                                text: qsTr("/path/to/video.mp4")
                            }
                        }
                    }
                    Pill {
                        id: checkButton
                        text: qsTr("Look at it")
                        enabledLook: pathInput.text.trim() !== ""
                        onClicked: {
                            saverPage.backend.inspect(pathInput.text)
                            saverPage.message = ""
                        }
                    }
                }

                // The video being looked at.
                Rectangle {
                    visible: addBox.chosen
                    width: parent.width
                    height: Math.max(150, candidateInfo.height + 28)
                    radius: 8
                    color: "#23252a"

                    Rectangle {
                        x: 14
                        y: 14
                        width: 84
                        height: 122
                        radius: 6
                        color: "#15171a"
                        Image {
                            anchors.fill: parent
                            visible: addBox.candidate.thumbnail !== undefined && addBox.candidate.thumbnail !== ""
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                            source: addBox.candidate.thumbnail !== undefined ? addBox.candidate.thumbnail : ""
                        }
                    }
                    Column {
                        id: candidateInfo
                        x: 114
                        y: 14
                        width: parent.width - 130
                        spacing: 6
                        Text {
                            width: parent.width
                            elide: Text.ElideMiddle
                            color: "white"
                            font.pixelSize: 16
                            font.bold: true
                            text: addBox.candidate.name !== undefined ? addBox.candidate.name : ""
                        }
                        Text {
                            visible: text !== ""
                            width: parent.width
                            color: "#c9ced6"
                            font.pixelSize: 14
                            text: addBox.candidate.state === "checking" ? qsTr("Reading the video…")
                                  : (addBox.candidate.details !== undefined ? addBox.candidate.details : "")
                        }
                        Repeater {
                            model: addBox.candidate.notes !== undefined ? addBox.candidate.notes : []
                            delegate: Text {
                                required property string modelData
                                width: candidateInfo.width
                                color: "#ffb74d"
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                                text: "• " + modelData
                            }
                        }
                        Text {
                            visible: addBox.candidate.state === "ready" && !addBox.candidate.canAdd
                            width: parent.width
                            color: "#ff8a80"
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                            text: addBox.candidate.why !== undefined ? addBox.candidate.why : ""
                        }
                        Rectangle {
                            visible: addBox.candidate.state === "ready" && addBox.candidate.canAdd
                            width: parent.width
                            height: 34
                            radius: 6
                            color: "#16181b"
                            border.width: 1
                            border.color: newTitle.activeFocus ? "#4caf50" : "#44ffffff"
                            TextInput {
                                id: newTitle
                                anchors.fill: parent
                                anchors.margins: 8
                                color: "white"
                                font.pixelSize: 15
                                clip: true
                                maximumLength: 60
                                Accessible.name: qsTr("Title of the new video")
                                Text {
                                    visible: newTitle.text === "" && !newTitle.activeFocus
                                    color: "#7d8591"
                                    font.pixelSize: 15
                                    text: addBox.candidate.suggestedTitle !== undefined ? addBox.candidate.suggestedTitle : ""
                                }
                            }
                        }
                        Row {
                            spacing: 10
                            Pill {
                                selected: true
                                text: qsTr("Add as it is")
                                visible: addBox.candidate.state === "ready"
                                enabledLook: addBox.candidate.canAdd === true
                                onClicked: {
                                    saverPage.answered(saverPage.backend.addCandidate(newTitle.text), qsTr("Added. galleond reads the change by itself."))
                                    newTitle.text = ""
                                    pathInput.text = ""
                                }
                            }
                            Pill {
                                text: qsTr("Cut to the module…")
                                visible: addBox.candidate.state === "ready" && addBox.candidate.canCut === true
                                selected: addBox.cropping
                                onClicked: addBox.cropping = !addBox.cropping
                            }
                            Pill {
                                text: qsTr("Cancel")
                                onClicked: {
                                    saverPage.backend.clearCandidate()
                                    newTitle.text = ""
                                }
                            }
                        }
                        Text {
                            visible: addBox.candidate.state === "ready" && addBox.candidate.canCut === false && addBox.candidate.cutWhy !== undefined && addBox.candidate.cutWhy !== ""
                            width: parent.width
                            wrapMode: Text.Wrap
                            color: "#ffb74d"
                            font.pixelSize: 13
                            text: addBox.candidate.cutWhy !== undefined ? addBox.candidate.cutWhy : ""
                        }
                    }
                }

                // The part of the video that goes to the module.
                Loader {
                    active: addBox.cropping && addBox.candidate.canCut === true
                    visible: active
                    sourceComponent: Component {
                        CropEditor {
                            id: cropEditor
                            backend: saverPage.backend
                            preview: addBox.candidate.thumbnail
                            sourceWidth: addBox.candidate.width
                            sourceHeight: addBox.candidate.height
                            canvasWidth: addBox.candidate.canvasWidth
                            canvasHeight: addBox.candidate.canvasHeight
                            identity: addBox.candidate.path
                            screenRegion: addBox.candidate.screen
                            keyRegions: addBox.candidate.keyRegions
                            help: qsTr("Drag the picture to move the frame, turn the wheel to come closer. The module shows what is inside the yellow frame: the cyan part is its screen, the squares are its 12 keys.")
                            Pill {
                                selected: true
                                text: qsTr("Cut and add to the list")
                                enabledLook: saverPage.backend.conversion.state !== "running"
                                onClicked: saverPage.answered(
                                    saverPage.backend.startConversion(cropEditor.crop.x, cropEditor.crop.y, cropEditor.crop.width, cropEditor.crop.height,
                                                                      newTitle.text !== "" ? newTitle.text : addBox.candidate.suggestedTitle),
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
                }

                // The cutting: how far it is.
                Rectangle {
                    visible: saverPage.backend.conversion.state === "running"
                    width: parent.width
                    height: 34
                    radius: 6
                    color: "#16181b"
                    Rectangle {
                        width: parent.width * saverPage.backend.conversion.fraction
                        height: parent.height
                        radius: 6
                        color: "#2e7d32"
                    }
                    Text {
                        anchors.centerIn: parent
                        color: "white"
                        font.pixelSize: 14
                        text: qsTr("Cutting… %1%").arg(Math.round(saverPage.backend.conversion.fraction * 100))
                    }
                }
                Row {
                    spacing: 10
                    visible: saverPage.backend.conversion.state === "running"
                    Pill {
                        danger: true
                        text: qsTr("Stop")
                        onClicked: saverPage.backend.cancelConversion()
                    }
                }
                Text {
                    visible: saverPage.backend.conversion.state === "failed"
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: "#ff8a80"
                    font.pixelSize: 13
                    text: saverPage.backend.conversion.message
                }
                Text {
                    visible: saverPage.backend.conversion.state === "done"
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: "#9be7a0"
                    font.pixelSize: 13
                    text: qsTr("Done: %1 is in the list. galleond reads the change by itself.").arg(saverPage.backend.conversion.message)
                }
            }
        }

        Text {
            width: parent.width
            color: "#7d8591"
            font.pixelSize: 13
            wrapMode: Text.Wrap
            text: qsTr("Every change is saved at once in %1 (the previous version is kept as .bak) and galleond reads it by itself.").arg(saverPage.backend.file)
        }
    }
}
