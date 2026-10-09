// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI

// One event on a line of a day: a tinted bar for one that lasts a day or
// more, a dot, title and time for a timed one.
Item {
    id: control
    objectName: "eventLine"

    property var item
    property var window
    readonly property bool bar: item.allDay || item.bar === true
    readonly property bool selected: window.isSelected(item)
    readonly property alias dragging: area.dragging

    signal dragMoved(point point)
    signal dragFinished(point start, point point)

    opacity: area.dragging ? 0.5 : 1
    Accessible.role: Accessible.Button
    Accessible.name: item.title

    FishUI.RoundedRectangle {
        anchors.fill: parent
        continuousCorners: false
        radius: 4
        color: control.selected ? control.item.color
             : control.bar ? Qt.alpha(control.item.color, FishUI.Theme.darkMode ? 0.32 : 0.2)
             : area.containsMouse ? Qt.alpha(FishUI.Theme.textColor, 0.05) : "transparent"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: control.bar ? 6 : 4
        anchors.rightMargin: 6
        spacing: 5

        FishUI.RoundedRectangle {
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            continuousCorners: false
            radius: 3
            color: control.selected ? FishUI.Theme.highlightedTextColor : control.item.color
            visible: !control.bar
        }

        FishUI.Label {
            Layout.fillWidth: true
            text: control.item.title.length > 0 ? control.item.title : qsTr("New Event")
            textFormat: Text.PlainText
            font.pointSize: 8
            font.weight: control.bar ? Font.Medium : Font.Normal
            color: control.selected ? FishUI.Theme.highlightedTextColor : FishUI.Theme.textColor
            elide: Text.ElideRight
        }

        FishUI.Label {
            text: control.item.startText || ""
            font.pointSize: 8
            color: control.selected ? FishUI.Theme.highlightedTextColor : FishUI.Theme.disabledTextColor
            visible: !control.bar && text.length > 0 && control.width >= 150
        }
    }

    EventMouseArea {
        id: area
        anchors.fill: parent
        item: control.item
        window: control.window
        onDragMoved: (point) => control.dragMoved(point)
        onDragFinished: (start, point) => control.dragFinished(start, point)
    }
}
