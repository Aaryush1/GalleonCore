// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// The wallpapers of the module's screen: choose one, add a picture of one's own (cut to the
// screen), delete one's own pictures. Choosing is applied at once when galleond runs and is kept
// for the next start. The project's pictures are never changed.
pragma ComponentBehavior: Bound

import QtQuick

Flickable {
    id: page

    required property var library
    required property var backend // for the crop maths
    property string style: "" // "" shows them all
    property string message: ""
    property bool messageIsError: false
    // For a picture of the page only: open the crop window on the first picture given with --wallpaper.
    readonly property var styles: {
        const seen = []
        for (const w of library.wallpapers) {
            if (seen.indexOf(w.style) < 0)
                seen.push(w.style)
        }
        return seen
    }
    readonly property var shown: library.wallpapers.filter(w => style === "" || w.style === style)

    contentWidth: width
    contentHeight: column.height + 20
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    function answered(error, done) {
        message = error === "" ? done : error
        messageIsError = error !== ""
    }

    Column {
        id: column
        width: page.width
        spacing: 14

        Text {
            color: "white"
            font.pixelSize: 18
            font.bold: true
            text: qsTr("Wallpapers")
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            color: "#9aa0a6"
            font.pixelSize: 13
            text: qsTr("The picture on the module's screen when no track plays. A click uses it: at once when galleond runs, and again at the next start.")
        }

        // The styles.
        Flow {
            width: parent.width
            spacing: 6
            Pill {
                text: qsTr("All")
                selected: page.style === ""
                onClicked: page.style = ""
            }
            Repeater {
                model: page.styles
                delegate: Pill {
                    required property string modelData
                    text: modelData
                    selected: page.style === modelData
                    onClicked: page.style = modelData
                }
            }
        }

        Flow {
            width: parent.width
            spacing: 12
            Repeater {
                model: page.shown
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    property bool confirming: false
                    readonly property bool inUse: page.library.current === modelData.name
                    width: 220
                    height: 156
                    radius: 8
                    color: "#23252a"
                    border.width: card.inUse ? 3 : 0
                    border.color: "#ff00ff"

                    Timer {
                        id: forget
                        interval: 4000
                        onTriggered: card.confirming = false
                    }

                    Image {
                        x: 6
                        y: 6
                        width: parent.width - 12
                        height: 108
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        source: card.modelData.thumbnail
                        sourceSize.width: 320
                        sourceSize.height: 180
                    }
                    MouseArea {
                        x: 0
                        y: 0
                        width: parent.width
                        height: 114
                        onClicked: page.answered(page.library.apply(card.modelData.name), qsTr("“%1” is in use.").arg(card.modelData.name))
                    }
                    Text {
                        x: 8
                        y: 118
                        width: parent.width - 16 - (card.modelData.mine ? deleteButton.width + 6 : 0)
                        elide: Text.ElideMiddle
                        color: card.inUse ? "#ff80ff" : "#c9ced6"
                        font.pixelSize: 12
                        font.bold: card.inUse
                        text: (card.inUse ? qsTr("In use · ") : "") + card.modelData.name.split("/").pop()
                    }
                    Text {
                        x: 8
                        y: 136
                        color: "#7d8591"
                        font.pixelSize: 11
                        text: card.modelData.mine ? qsTr("yours") : card.modelData.style
                    }
                    Pill {
                        id: deleteButton
                        visible: card.modelData.mine
                        danger: true
                        height: 24
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        y: 118
                        text: card.confirming ? qsTr("Delete?") : qsTr("Delete")
                        onClicked: {
                            if (!card.confirming) {
                                card.confirming = true
                                forget.restart()
                                return
                            }
                            page.answered(page.library.removeMine(card.modelData.name), qsTr("Deleted."))
                        }
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: card.modelData.name
                }
            }
        }

        Text {
            visible: page.library.wallpapers.length === 0
            width: parent.width
            wrapMode: Text.Wrap
            color: "#ffb74d"
            font.pixelSize: 13
            text: qsTr("No wallpaper was found. Add a picture below, or put pictures in a folder named after a style under %1.").arg("~/.local/share/galleoncore/wallpaper")
        }

        Text {
            width: parent.width
            visible: page.message !== ""
            wrapMode: Text.Wrap
            color: page.messageIsError ? "#ff8a80" : "#9be7a0"
            font.pixelSize: 13
            text: page.message
        }

        // ---- adding a picture of one's own
        Rectangle {
            id: addBox
            readonly property var candidate: page.library.candidate
            readonly property bool chosen: candidate.chosen !== undefined
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
                        page.library.inspect(event.urls[0].toString())
                        page.message = ""
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
                    text: qsTr("Add a picture of your own")
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: "#9aa0a6"
                    font.pixelSize: 13
                    text: qsTr("Drop a picture here, or write its path. You choose the part that goes to the screen; the result is saved in your own folder, and the original is not touched. The pictures you add stay on your computer.")
                }
                Row {
                    spacing: 10
                    Field {
                        id: pathField
                        width: addColumn.width - lookButton.width - 10
                        placeholder: qsTr("/path/to/picture.jpg")
                        maximumLength: 400
                    }
                    Pill {
                        id: lookButton
                        text: qsTr("Look at it")
                        enabledLook: pathField.input.text.trim() !== ""
                        onClicked: {
                            page.library.inspect(pathField.input.text)
                            page.message = ""
                        }
                    }
                }

                Text {
                    visible: addBox.chosen && addBox.candidate.state === "unusable"
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: "#ff8a80"
                    font.pixelSize: 13
                    text: addBox.candidate.why !== undefined ? addBox.candidate.why : ""
                }

                Loader {
                    active: addBox.chosen && addBox.candidate.state === "ready"
                    visible: active
                    sourceComponent: Component {
                        CropEditor {
                            id: cropEditor
                            backend: page.backend
                            preview: addBox.candidate.preview
                            sourceWidth: addBox.candidate.width
                            sourceHeight: addBox.candidate.height
                            canvasWidth: addBox.candidate.canvasWidth
                            canvasHeight: addBox.candidate.canvasHeight
                            identity: addBox.candidate.chosen
                            help: qsTr("Drag the picture to move the frame, turn the wheel to come closer. The module's screen shows what is inside the yellow frame.")
                            Field {
                                id: nameField
                                width: parent.width
                                maximumLength: 60
                                placeholder: addBox.candidate.suggestedName !== undefined ? addBox.candidate.suggestedName : qsTr("Name")
                            }
                            Row {
                                spacing: 10
                                Pill {
                                    selected: true
                                    text: qsTr("Save it")
                                    onClicked: {
                                        const name = nameField.input.text
                                        page.answered(page.library.addCandidate(cropEditor.crop.x, cropEditor.crop.y, cropEditor.crop.width,
                                                                                cropEditor.crop.height, name),
                                                      qsTr("Saved among your pictures."))
                                        pathField.input.text = ""
                                    }
                                }
                                Pill {
                                    text: qsTr("Cancel")
                                    onClicked: page.library.clearCandidate()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
