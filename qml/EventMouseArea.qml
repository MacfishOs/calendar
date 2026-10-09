// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Pointer handling every event shape shares: click selects, double click
// edits, right click opens the event menu, and a press that moves drags.
MouseArea {
    id: area

    property var item
    property var window
    property bool dragging: false
    property point pressPoint

    signal dragMoved(point point)
    signal dragFinished(point start, point point)

    acceptedButtons: Qt.LeftButton | Qt.RightButton
    hoverEnabled: true
    preventStealing: dragging

    onPressed: (mouse) => {
        pressPoint = Qt.point(mouse.x, mouse.y)
        dragging = false
        window.selectEvent(item)
        window.focusView()
    }
    onPositionChanged: (mouse) => {
        if (!pressed || !(pressedButtons & Qt.LeftButton))
            return
        if (!dragging && Math.hypot(mouse.x - pressPoint.x, mouse.y - pressPoint.y) >= 6)
            dragging = true
        if (dragging)
            dragMoved(Qt.point(mouse.x, mouse.y))
    }
    onReleased: (mouse) => {
        if (dragging) {
            dragging = false
            dragFinished(pressPoint, Qt.point(mouse.x, mouse.y))
        }
    }
    onCanceled: dragging = false
    onClicked: (mouse) => {
        if (mouse.button === Qt.RightButton)
            window.showEventMenu(item)
    }
    onDoubleClicked: (mouse) => {
        if (mouse.button === Qt.LeftButton)
            window.editEvent(item)
    }
}
