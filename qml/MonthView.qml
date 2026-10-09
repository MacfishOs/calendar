// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI
import "Dates.js" as Dates

// Six weeks around the current month: events that last a day or more as bars
// across their days, timed ones as lines under them.
Item {
    id: view
    objectName: "monthView"

    required property var window
    // Hidden by opacity, not visibility: showing hundreds of items again rebuilds their nodes.
    property bool live: false

    readonly property int firstDay: App.firstDayOfWeek
    readonly property date gridStart: Dates.monthGridStart(window.currentDate, firstDay)
    readonly property real headerHeight: 30
    readonly property real gutter: App.showWeekNumbers ? 28 : 0
    readonly property real cellWidth: (width - gutter) / 7
    readonly property real rowHeight: (height - headerHeight) / 6
    readonly property real dayLabelHeight: 28
    readonly property real lineHeight: 20
    readonly property int lanes: Math.max(1, Math.floor((rowHeight - dayLabelHeight - 4) / lineHeight))
    // Laid out again only when shown and something it shows changed, so a
    // hidden view costs nothing and showing an unchanged one is free.
    property var layout: ({ bars: [], more: [] })
    readonly property string layoutKey: [App.revision, gridStart.getTime(), lanes].join()
    property string laidOutKey: ""

    function refresh(force) {
        if ((!live && !force) || layoutKey === laidOutKey)
            return
        laidOutKey = layoutKey
        layout = App.monthLayout(gridStart, 6, lanes)
    }

    onLayoutKeyChanged: refresh()
    onLiveChanged: refresh()
    // Laid out once while preloaded, so the first switch to it is quick too.
    Component.onCompleted: refresh(true)
    readonly property color lineColor: Qt.alpha(FishUI.Theme.textColor, 0.07)

    // The day under a point of the view, or null outside the grid.
    function dateAt(x, y) {
        if (x < gutter || y < headerHeight || x >= width || y >= height)
            return null
        const column = Math.min(6, Math.floor((x - gutter) / cellWidth))
        const row = Math.min(5, Math.floor((y - headerHeight) / rowHeight))
        return Dates.addDays(gridStart, row * 7 + column)
    }

    // A wheel step or a touchpad swipe turns the month.
    WheelHandler {
        id: wheel
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        property real travel: 0
        property bool spent: false
        onWheel: (event) => {
            const delta = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 120 * 60
            settle.restart()
            if (spent)
                return
            travel += delta
            if (Math.abs(travel) >= 60) {
                view.window.step(travel > 0 ? -1 : 1)
                travel = 0
                spent = event.pixelDelta.y !== 0
            }
        }
    }

    Timer {
        id: settle
        interval: 200
        onTriggered: {
            wheel.travel = 0
            wheel.spent = false
        }
    }

    Repeater {
        model: 7

        FishUI.Label {
            x: view.gutter + index * view.cellWidth
            width: view.cellWidth
            height: view.headerHeight
            rightPadding: 12
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            text: Qt.locale().standaloneDayName(Dates.weekdayOfColumn(index, view.firstDay), Locale.ShortFormat)
            font.pointSize: 9
            color: FishUI.Theme.disabledTextColor
            elide: Text.ElideRight
        }
    }

    Rectangle {
        y: view.headerHeight
        width: view.width
        height: 1 / FishUI.Dpi.ratio
        color: view.lineColor
    }

    readonly property int todayIndex: indexOf(App.today)
    readonly property int selectedIndex: indexOf(window.currentDate)
    readonly property int monthFirst: Dates.daysBetween(gridStart, Dates.monthStart(window.currentDate))
    readonly property int monthLast: monthFirst + new Date(window.currentDate.getFullYear(),
                                                           window.currentDate.getMonth() + 1, 0).getDate() - 1

    function indexOf(date) {
        const index = Dates.daysBetween(gridStart, date)
        return index >= 0 && index < 42 ? index : -1
    }

    // The first of a month carries the month's name.
    function dayText(index) {
        const date = Dates.addDays(gridStart, index)
        if (date.getDate() !== 1)
            return String(date.getDate())
        const language = Qt.locale().name.split("_")[0]
        if (language === "zh" || language === "ja")
            return (date.getMonth() + 1) + "月1日"
        return Qt.locale().standaloneMonthName(date.getMonth(), Locale.ShortFormat) + " 1"
    }

    function plateWidth(textWidth) {
        return Math.max(22, textWidth + 10)
    }

    TextMetrics {
        id: capMetrics
        font.pointSize: 9
        text: "H"
    }

    TextMetrics {
        id: todayMetrics
        font.pointSize: 9
        font.weight: Font.DemiBold
        text: view.todayIndex >= 0 ? view.dayText(view.todayIndex) : ""
    }

    // Weekend columns, as the language counts them.
    Repeater {
        model: 7

        Rectangle {
            x: view.gutter + index * view.cellWidth
            y: view.headerHeight
            width: view.cellWidth
            height: view.height - y
            color: Qt.alpha(FishUI.Theme.textColor, FishUI.Theme.darkMode ? 0.03 : 0.02)
            visible: App.weekendDays.indexOf(Dates.weekdayOfColumn(index, view.firstDay)) >= 0
        }
    }

    Rectangle {
        x: view.gutter + (view.selectedIndex % 7) * view.cellWidth
        y: view.headerHeight + Math.floor(view.selectedIndex / 7) * view.rowHeight
        width: view.cellWidth
        height: view.rowHeight
        color: Qt.alpha(FishUI.Theme.highlightColor, FishUI.Theme.darkMode ? 0.08 : 0.04)
        visible: view.selectedIndex >= 0
    }

    Repeater {
        model: 5

        Rectangle {
            y: view.headerHeight + (index + 1) * view.rowHeight
            width: view.width
            height: 1 / FishUI.Dpi.ratio
            color: view.lineColor
        }
    }

    Repeater {
        model: view.gutter > 0 ? 7 : 6

        Rectangle {
            x: view.gutter + (index + (view.gutter > 0 ? 0 : 1)) * view.cellWidth
            y: view.headerHeight
            width: 1 / FishUI.Dpi.ratio
            height: view.height - y
            color: view.lineColor
        }
    }

    FishUI.RoundedRectangle {
        x: view.gutter + (view.todayIndex % 7 + 1) * view.cellWidth - 6 - width
        y: view.headerHeight + Math.floor(view.todayIndex / 7) * view.rowHeight + 4
        width: view.plateWidth(todayMetrics.advanceWidth)
        height: 22
        radius: height / 2
        continuousCorners: false
        color: FishUI.Theme.highlightColor
        visible: view.todayIndex >= 0
    }

    // One plain Text per day, everything else shared: a month is built on every switch to it.
    Repeater {
        model: 42

        Text {
            readonly property bool inMonth: index >= view.monthFirst && index <= view.monthLast
            readonly property bool today: index === view.todayIndex
            readonly property real cellRight: view.gutter + (index % 7 + 1) * view.cellWidth

            // Centred on where the today plate would be, capitals on its middle.
            x: cellRight - 6 - view.plateWidth(implicitWidth) / 2 - implicitWidth / 2
            y: Math.round((view.headerHeight + Math.floor(index / 7) * view.rowHeight + 4 + 11
                           + capMetrics.tightBoundingRect.height / 2 - baselineOffset) * FishUI.Dpi.ratio) / FishUI.Dpi.ratio
            renderType: FishUI.Theme.renderType
            text: view.dayText(index)
            font.pointSize: 9
            font.weight: today ? Font.DemiBold : Font.Normal
            color: today ? FishUI.Theme.highlightedTextColor
                 : inMonth ? FishUI.Theme.textColor : FishUI.Theme.disabledTextColor
            opacity: inMonth || today ? 1 : 0.6
        }
    }

    MouseArea {
        anchors.fill: parent
        anchors.topMargin: view.headerHeight
        anchors.leftMargin: view.gutter
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: (mouse) => {
            view.window.currentDate = view.dateAt(mouse.x + view.gutter, mouse.y + view.headerHeight)
            view.window.selectEvent(null)
        }
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton)
                view.window.newEventOn(view.dateAt(mouse.x + view.gutter, mouse.y + view.headerHeight))
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton)
                view.window.showDayMenu(view.dateAt(mouse.x + view.gutter, mouse.y + view.headerHeight))
        }
    }

    Repeater {
        model: App.showWeekNumbers ? 6 : 0

        FishUI.Label {
            x: 0
            y: view.headerHeight + index * view.rowHeight + 4
            width: view.gutter
            height: 22
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: App.weekNumber(Dates.addDays(view.gridStart, index * 7))
            font.pointSize: 8
            color: FishUI.Theme.disabledTextColor
        }
    }

    // Where a dragged event would land.
    Rectangle {
        id: dropTarget
        property var date: null
        x: date ? view.gutter + (Dates.daysBetween(view.gridStart, date) % 7) * view.cellWidth : 0
        y: date ? view.headerHeight + Math.floor(Dates.daysBetween(view.gridStart, date) / 7) * view.rowHeight : 0
        width: view.cellWidth
        height: view.rowHeight
        color: Qt.alpha(FishUI.Theme.highlightColor, 0.12)
        border.width: 1
        border.color: Qt.alpha(FishUI.Theme.highlightColor, 0.5)
        visible: date !== null
    }

    Repeater {
        model: view.layout.bars

        EventLine {
            required property var modelData

            item: modelData
            window: view.window
            x: view.gutter + modelData.column * view.cellWidth + (modelData.continuesBefore ? 0 : 4)
            y: view.headerHeight + modelData.row * view.rowHeight + view.dayLabelHeight + 2
               + modelData.lane * view.lineHeight
            width: modelData.span * view.cellWidth - (modelData.continuesBefore ? 0 : 4)
                   - (modelData.continuesAfter ? 0 : 4)
            height: view.lineHeight - 2

            onDragMoved: (point) => {
                const position = mapToItem(view, point.x, point.y)
                dropTarget.date = view.dateAt(position.x, position.y)
            }
            onDragFinished: (start, point) => {
                const from = view.dateAt(mapToItem(view, start.x, start.y).x, mapToItem(view, start.x, start.y).y)
                const to = dropTarget.date
                dropTarget.date = null
                if (from && to && !Dates.sameDay(from, to)) {
                    const days = Dates.daysBetween(from, to)
                    view.window.moveEvent(modelData, days, 0, days, 0)
                }
            }
        }
    }

    Repeater {
        model: view.layout.more

        FishUI.Label {
            required property var modelData

            x: view.gutter + modelData.column * view.cellWidth + 10
            y: view.headerHeight + modelData.row * view.rowHeight + view.dayLabelHeight + 2
               + (view.lanes - 1) * view.lineHeight
            width: view.cellWidth - 20
            height: view.lineHeight - 2
            verticalAlignment: Text.AlignVCenter
            text: qsTr("%1 more").arg(modelData.count)
            font.pointSize: 8
            font.weight: Font.Medium
            color: moreArea.containsMouse ? FishUI.Theme.highlightColor : FishUI.Theme.disabledTextColor
            elide: Text.ElideRight

            MouseArea {
                id: moreArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: view.window.showDay(modelData.date)
            }
        }
    }
}
