// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// A value changed by two buttons, in steps, between two limits.
import QtQuick

Row {
    id: stepper
    property int value: 0
    property int from: 0
    property int to: 10
    property int step: 1
    property string unit: ""
    signal picked(int value)
    spacing: 8
    Pill {
        text: "−"
        enabledLook: stepper.value > stepper.from
        onClicked: stepper.picked(Math.max(stepper.from, stepper.value - stepper.step))
    }
    Text {
        width: 90
        height: 32
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: "white"
        font.pixelSize: 16
        font.bold: true
        text: stepper.value + (stepper.unit !== "" ? " " + stepper.unit : "")
    }
    Pill {
        text: "+"
        enabledLook: stepper.value < stepper.to
        onClicked: stepper.picked(Math.min(stepper.to, stepper.value + stepper.step))
    }
}
