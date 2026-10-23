// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
Flickable {
    id: page
    objectName: "widgetsPage"
    required property var widgets
    property int selected: 0
    readonly property var entry: widgets.layout[selected]
    readonly property var names: ({clock: qsTr("Clock and date"), weather: qsTr("Weather"), cpu: qsTr("CPU"), memory: qsTr("RAM"), temperature: qsTr("CPU temperature"), downloads: qsTr("Downloads"), fps: qsTr("Game FPS")})
    contentWidth: width
    contentHeight: content.height
    clip: true
    function applyEdits() {
        const name = city.input.text, lat = latitude.input.text, lon = longitude.input.text
        const source = fpsSource.input.text, process = fpsProcess.input.text
        if (name !== (widgets.location.city || "") && !widgets.setCity(name)) return
        if (lat !== "" || lon !== "" || widgets.location.latitude !== undefined) {
            if ((lat !== String(widgets.location.latitude) || lon !== String(widgets.location.longitude))
                && !widgets.setLocation(lat, lon)) return
        }
        if (source !== (widgets.fpsConfig.source || "") || process !== (widgets.fpsConfig.process || "")) {
            if (!widgets.setFpsSource(source, process)) return
        }
        widgets.apply()
    }
    function edit(key, value) {
        const e = Object.assign({}, entry)
        e[key] = value
        e.x = Math.max(0, Math.min(e.x, 720 - e.width))
        e.y = Math.max(0, Math.min(e.y, 384 - e.height))
        widgets.setWidget(e.id, e.enabled, e.x, e.y, e.width, e.height)
    }
    Column {
        id: content
        width: page.width
        spacing: 14
        Text { text: qsTr("Home screen widgets · 720 × 384"); color: "white"; font.pixelSize: 20; font.bold: true }
        Text { width: parent.width; wrapMode: Text.Wrap; color: "#c9ced6"; text: qsTr("Select a widget, drag it in the preview, or adjust its position and size. Click Apply to save changes and show them on the keyboard screen. Overlapping widgets are drawn in list order.") }
        Row {
            spacing: 12
            Pill {
                objectName: "applyWidgets"
                text: qsTr("Apply")
                selected: page.widgets.pending
                onClicked: page.applyEdits()
            }
            Text {
                height: 32
                verticalAlignment: Text.AlignVCenter
                text: page.widgets.pending ? qsTr("Preview · changes not applied") : qsTr("Layout applied")
                color: page.widgets.pending ? "#ffb74d" : "#9be7a0"
            }
        }
        Row {
            spacing: 6
            Repeater {
                model: page.widgets.layout
                delegate: Pill {
                    required property var modelData
                    required property int index
                    text: (modelData.enabled ? "● " : "○ ") + page.names[modelData.id]
                    onClicked: page.selected = index
                }
            }
        }
        Row {
            spacing: 22
        Rectangle {
            id: preview
            width: Math.min(720, page.width * 0.62)
            height: width * 384 / 720
            color: "#151d26"
            border.color: "#4b6574"
            Loader {
                id: overlay
                anchors.fill: parent
                source: "qrc:/homewidgets/HomeOverlay.qml"
                onLoaded: {
                    item.layout = Qt.binding(() => page.widgets.layout)
                    item.values = Qt.binding(() => page.widgets.values)
                }
            }
            Repeater {
                model: page.widgets.layout
                delegate: Rectangle {
                    id: handle
                    required property var modelData
                    required property int index
                    x: modelData.x * preview.width / 720
                    y: modelData.y * preview.height / 384
                    width: modelData.width * preview.width / 720
                    height: modelData.height * preview.height / 384
                    visible: modelData.enabled
                    color: "transparent"
                    border.color: page.selected === index ? "#4fc3f7" : "#446c8290"
                    border.width: page.selected === index ? 2 : 1
                    MouseArea {
                        anchors.fill: parent
                        drag.target: handle
                        drag.minimumX: 0; drag.minimumY: 0
                        drag.maximumX: preview.width - handle.width
                        drag.maximumY: preview.height - handle.height
                        onPressed: page.selected = handle.index
                        onReleased: {
                            const id = handle.modelData.id
                            const x = Math.round(handle.x * 720 / preview.width)
                            const y = Math.round(handle.y * 384 / preview.height)
                            const w = handle.modelData.width, h = handle.modelData.height
                            handle.x = Qt.binding(() => handle.modelData.x * preview.width / 720)
                            handle.y = Qt.binding(() => handle.modelData.y * preview.height / 384)
                            page.widgets.setWidget(id, true, x, y, w, h)
                        }
                    }
                }
            }
            Timer { interval: 1000; running: page.visible; repeat: true; onTriggered: if (overlay.item) overlay.item.now = new Date() }
        }
            Column {
                width: page.width - preview.width - 22
                spacing: 16
                Text { text: page.names[page.entry.id]; color: "white"; font.pixelSize: 18; font.bold: true }
                Pill { text: page.entry.enabled ? qsTr("Hide widget") : qsTr("Show widget"); onClicked: page.edit("enabled", !page.entry.enabled) }
                Grid {
                    columns: 2; columnSpacing: 14; rowSpacing: 14
                    Text { text: "X"; color: "white" }
                    Stepper { value: page.entry.x; to: 720 - page.entry.width; step: 5; onPicked: v => page.edit("x", v) }
                    Text { text: "Y"; color: "white" }
                    Stepper { value: page.entry.y; to: 384 - page.entry.height; step: 5; onPicked: v => page.edit("y", v) }
                    Text { text: qsTr("Width"); color: "white" }
                    Stepper { value: page.entry.width; from: 100; to: 720; step: 10; onPicked: v => page.edit("width", v) }
                    Text { text: qsTr("Height"); color: "white" }
                    Stepper { value: page.entry.height; from: 40; to: 384; step: 5; onPicked: v => page.edit("height", v) }
                }
                Row {
                    spacing: 14
                    Text { text: qsTr("Text size"); color: "white" }
                    Stepper {
                        value: page.entry.textSize || 32
                        from: 12; to: 64; step: 2; unit: "px"
                        onPicked: v => page.widgets.setWidgetTextSize(page.entry.id, v)
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: "#aebac6"
                    text: qsTr("Text fits inside the card. Increase its height/width too if the chosen size does not fit.")
                }
                Pill { text: qsTr("Restore default layout"); onClicked: page.widgets.reset() }
            }
        }
        Row {
            visible: page.entry.id === "weather"
            spacing: 12
            Field {
                id: city
                value: page.widgets.location.city || ""
                placeholder: qsTr("City name")
                maximumLength: 80
                onEdited: text => page.widgets.setCity(text)
            }
            Field { id: latitude; value: page.widgets.location.latitude === undefined ? "" : String(page.widgets.location.latitude); placeholder: qsTr("Latitude (e.g. 48.85)") }
            Field { id: longitude; value: page.widgets.location.longitude === undefined ? "" : String(page.widgets.location.longitude); placeholder: qsTr("Longitude (e.g. 2.35)") }
            Pill { text: qsTr("Set location"); onClicked: page.widgets.setLocation(latitude.input.text, longitude.input.text) }
        }
        Row {
            visible: page.entry.id === "fps"
            spacing: 12
            Field {
                id: fpsSource
                width: Math.min(560, page.width * 0.5)
                maximumLength: 4096
                value: page.widgets.fpsConfig.source || ""
                placeholder: qsTr("FPS CSV file or log folder (absolute path)")
                onEdited: text => page.widgets.setFpsSource(text, fpsProcess.input.text)
            }
            Field {
                id: fpsProcess
                maximumLength: 260
                value: page.widgets.fpsConfig.process || ""
                placeholder: qsTr("Game executable (optional)")
                onEdited: text => page.widgets.setFpsSource(fpsSource.input.text, text)
            }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#aebac6"
            text: page.entry.id === "fps" ? qsTr("Game FPS from a live MangoHud CSV (Linux) or PresentMon --v1_metrics CSV (Windows). Configure the log file/folder, enable capture in the game tool, then Apply. No capture data for 10 seconds: waiting. Use a dedicated game capture or enter its executable to filter PresentMon. Setup: docs/HOME_WIDGETS.md.")
                : page.entry.id === "weather" ? qsTr("Weather data: Open-Meteo (CC BY 4.0). Enabling this widget sends the configured coordinates to api.open-meteo.com every 15 minutes.")
                : page.entry.id === "downloads" ? qsTr("Detects .part and .crdownload files in the system Downloads folder (Firefox and Chromium browsers). Partial files may be paused or abandoned. Browser identity, exact progress and custom download folders are not available yet.")
                : page.entry.id === "temperature" ? qsTr("Linux: CPU sensors via hwmon (Intel/AMD). Windows: temperature requires an additional sensor provider; unavailable for now.")
                : qsTr("CPU and memory: native Linux / Windows data, refreshed every 2 seconds.")
        }
        Text { width: parent.width; wrapMode: Text.Wrap; text: page.widgets.error; color: "#ffb74d" }
    }
}
