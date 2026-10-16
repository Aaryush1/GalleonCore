// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.plasma.plasmoid

// TODO: load the shared widgets/Clock.qml installed by GalleonCore
// (share/galleoncore/widgets) instead of this placeholder, and talk to
// galleond over D-Bus (org.galleoncore.Deck).
PlasmoidItem {
    fullRepresentation: Item {
        implicitWidth: 320
        implicitHeight: 180

        Text {
            anchors.centerIn: parent
            text: Qt.formatTime(new Date(), "hh:mm")
            font.pixelSize: parent.height * 0.35
        }
    }
}
