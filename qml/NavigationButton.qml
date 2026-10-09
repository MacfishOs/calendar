// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import FishUI as FishUI

// A pill on the calendar's surface: a round one for an icon, a wider one for text.
AbstractButton {
    id: control

    property string iconName

    implicitWidth: iconName.length > 0 ? 30 : label.implicitWidth + 28
    implicitHeight: 30
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: text

    background: FishUI.RoundedRectangle {
        continuousCorners: false
        radius: height / 2
        color: Qt.alpha(FishUI.Theme.textColor, control.down ? 0.12 : control.hovered ? 0.08 : 0.05)
    }

    contentItem: Item {
        FishUI.Icon {
            anchors.centerIn: parent
            width: 16
            height: 16
            name: control.iconName
            visible: control.iconName.length > 0
        }

        FishUI.Label {
            id: label
            anchors.centerIn: parent
            text: control.text
            font.pointSize: 9
            font.weight: Font.Medium
            visible: control.iconName.length === 0
        }
    }
}
