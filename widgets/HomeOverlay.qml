// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
Item {
    id: root
    property var layout: []
    property var values: ({})
    property date now: new Date()
    property var locale: Qt.locale()
    property string timeFormat: locale.timeFormat(Locale.ShortFormat)
    property color textColor: "white"
    Repeater {
        model: root.layout
        delegate: Rectangle {
            id: card
            required property var modelData
            readonly property real textScale: (modelData.textSize || 32) / 32
            readonly property real textSize: (modelData.textSize || 32) * root.height / 384
            readonly property bool clock: modelData.id === "clock"
            x: modelData.x * root.width / 720
            y: modelData.y * root.height / 384
            width: modelData.width * root.width / 720
            height: modelData.height * root.height / 384
            visible: modelData.enabled
            color: clock ? "transparent" : "#db202528"
            radius: 10 * root.height / 384
            clip: true
            Column {
                anchors.fill: parent
                anchors.margins: card.clock ? 0 : 4 * root.height / 384
                spacing: 2 * root.height / 384
                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    height: parent.height * (card.clock ? 0.74 : 0.34)
                    color: root.textColor
                    font.pixelSize: card.clock ? Math.min(card.height * 0.68, card.width * 0.30) * card.textScale : card.textSize * 0.56
                    font.bold: card.clock
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 8
                    elide: Text.ElideRight
                    text: card.clock ? root.now.toLocaleTimeString(root.locale, root.timeFormat)
                        : ({weather: root.values.weatherCity || qsTr("Weather"), cpu: qsTr("CPU"), memory: qsTr("RAM"), temperature: qsTr("CPU temperature"), downloads: qsTr("Downloads"), fps: qsTr("Game FPS")})[card.modelData.id]
                }
                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    height: parent.height * (card.clock ? 0.24 : 0.62)
                    color: root.textColor
                    font.bold: !card.clock
                    opacity: card.clock ? 0.8 : 1
                    font.pixelSize: card.clock ? Math.min(card.height * 0.17, card.width * 0.065) * card.textScale : card.textSize
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 8
                    elide: Text.ElideRight
                    text: card.clock ? root.now.toLocaleDateString(root.locale, root.locale.dateFormat(Locale.LongFormat))
                        : (root.values[card.modelData.id] || qsTr("Unavailable"))
                }
            }
        }
    }
}
