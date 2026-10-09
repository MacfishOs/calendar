// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI

// A timed event on the hour grid: moved by dragging it, lengthened by
// dragging its bottom edge.
Item {
    id: control
    objectName: "eventBlock"

    property var item
    property var window
    property var gridView
    readonly property bool selected: window.isSelected(item)
    readonly property bool compact: height < 34

    Accessible.role: Accessible.Button
    Accessible.name: item.title

    FishUI.RoundedRectangle {
        anchors.fill: parent
        continuousCorners: false
        radius: 4
        color: control.selected ? control.item.color
                                : Qt.alpha(control.item.color, FishUI.Theme.darkMode ? 0.32 : 0.18)
    }

    FishUI.RoundedRectangle {
        x: 0
        y: 0
        width: 3
        height: parent.height
        continuousCorners: false
        radius: 1.5
        color: control.item.color
        visible: !control.selected
    }

    Column {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 4
        anchors.topMargin: control.compact ? 0 : 3
        clip: true

        FishUI.Label {
            width: parent.width
            height: control.compact ? control.height : implicitHeight
            verticalAlignment: control.compact ? Text.AlignVCenter : Text.AlignTop
            text: (control.item.title.length > 0 ? control.item.title : qsTr("New Event"))
                  + (control.compact && control.item.startText && control.width >= 140 ? "  " + control.item.startText : "")
            textFormat: Text.PlainText
            font.pointSize: 8
            font.weight: Font.Medium
            color: control.selected ? FishUI.Theme.highlightedTextColor : FishUI.Theme.textColor
            elide: Text.ElideRight
            // A word wider than a narrow block is elided, not broken or cut.
            wrapMode: control.compact || control.width < 80 ? Text.NoWrap : Text.WordWrap
            maximumLineCount: control.compact || control.width < 80 ? 1 : 2
        }

        FishUI.Label {
            width: parent.width
            text: control.item.continuesBefore ? qsTr("until %1").arg(control.item.endText)
                                               : control.item.startText + " – " + control.item.endText
            font.pointSize: 8
            color: control.selected ? FishUI.Theme.highlightedTextColor : FishUI.Theme.disabledTextColor
            elide: Text.ElideRight
            visible: !control.compact && control.height >= 40
        }

        FishUI.Label {
            width: parent.width
            text: control.item.location
            textFormat: Text.PlainText
            font.pointSize: 8
            color: control.selected ? FishUI.Theme.highlightedTextColor : FishUI.Theme.disabledTextColor
            elide: Text.ElideRight
            visible: !control.compact && control.height >= 58 && text.length > 0
        }
    }

    EventMouseArea {
        id: area
        anchors.fill: parent
        anchors.bottomMargin: resizer.visible ? resizer.height : 0
        item: control.item
        window: control.window

        onDragMoved: (point) => {
            const grid = control.gridView
            const start = control.mapToItem(grid, pressPoint.x, pressPoint.y)
            const now = control.mapToItem(grid, point.x, point.y)
            grid.dragKey = control.item.key
            grid.dragRecurrenceKey = control.item.recurrenceKey
            grid.dragDays = grid.columnAt(now.x) - grid.columnAt(start.x)
            grid.dragMinutes = grid.snap((now.y - start.y) / grid.hourHeight * 60)
        }
        onDragFinished: control.gridView.finishDrag(control.item)
    }

    // The end of an event that ends on this day can be dragged.
    MouseArea {
        id: resizer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 6
        visible: !control.item.continuesAfter && !control.item.allDay
        cursorShape: Qt.SizeVerCursor
        preventStealing: true

        property real startY: 0

        onPressed: (mouse) => {
            startY = mapToItem(control.gridView, mouse.x, mouse.y).y
            control.window.selectEvent(control.item)
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const grid = control.gridView
            const y = mapToItem(grid, mouse.x, mouse.y).y
            const minutes = grid.snap((y - startY) / grid.hourHeight * 60)
            const length = control.item.endMinute - control.item.startMinute
            grid.dragKey = control.item.key
            grid.dragRecurrenceKey = control.item.recurrenceKey
            // At least one step long, and not past midnight.
            grid.resizeMinutes = Math.max(grid.step - length,
                                          Math.min(24 * 60 - control.item.endMinute, minutes))
        }
        onReleased: control.gridView.finishDrag(control.item)
    }
}
