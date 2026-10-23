// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
Rectangle {
    id: root
    property var stateData: ({})
    implicitWidth: 720
    implicitHeight: 384
    color: "#101a25"
    clip: true
    Image {
        anchors.fill: parent
        source: root.stateData.image || ""
        asynchronous: false
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: 1440
        sourceSize.height: 768
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#80000000" }
            GradientStop { position: 0.45; color: "#25000000" }
            GradientStop { position: 1; color: "#ef081018" }
        }
    }
    Text {
        x: root.width * 0.04; y: root.height * 0.08
        width: root.width * 0.92; height: root.height * 0.22
        text: root.stateData.title || qsTr("Game")
        textFormat: Text.PlainText
        color: "white"; font.bold: true; font.pixelSize: root.height * 0.10
        elide: Text.ElideRight
    }
    Column {
        x: root.width * 0.04; y: root.height * 0.53
        width: root.width * 0.27
        Text { text: "FPS"; color: "#80e5d1"; font.pixelSize: root.height * 0.065 }
        Text {
            width: parent.width; height: root.height * 0.28
            text: root.stateData.fps || "—"; color: "white"
            font.bold: true; font.pixelSize: root.height * 0.24
            fontSizeMode: Text.Fit; minimumPixelSize: 18
        }
    }
    CarGauge {
        x: root.width * 0.35; y: root.height * 0.35
        width: root.width * 0.29; height: root.height * 0.59
        label: "CPU"
        value: root.stateData.cpuLoad === undefined ? -1 : root.stateData.cpuLoad
        temperature: root.stateData.cpu || "—"
    }
    CarGauge {
        x: root.width * 0.67; y: root.height * 0.35
        width: root.width * 0.29; height: root.height * 0.59
        label: "GPU"; accent: "#90b8ff"
        value: root.stateData.gpuLoad === undefined ? -1 : root.stateData.gpuLoad
        temperature: root.stateData.gpu || "—"
    }
}
