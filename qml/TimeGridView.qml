// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI
import "Dates.js" as Dates

// Days side by side over the hours, the week view with seven and the day
// view with one: all-day events in a strip on top, timed ones as blocks.
Item {
    id: view
    objectName: days === 1 ? "dayView" : "weekView"

    required property var window
    // Hidden by opacity, not visibility: showing hundreds of items again rebuilds their nodes.
    property bool live: false
    property int days: 7

    readonly property date rangeStart: days === 7 ? Dates.weekStart(window.currentDate, App.firstDayOfWeek)
                                                  : window.currentDate
    readonly property real gutter: 56
    // The day view's title names the day already.
    readonly property real headerHeight: days === 1 ? 0 : 44
    readonly property real hourHeight: 48
    readonly property real lineHeight: 20
    readonly property int maximumLanes: 3
    readonly property real columnWidth: (width - gutter) / days
    // Laid out again only when shown and something it shows changed.
    property var layout: ({ bars: [], more: [], lanes: 0, blocks: [] })
    readonly property string layoutKey: [App.revision, rangeStart.getTime(), days].join()
    property string laidOutKey: ""

    function refresh(force) {
        if ((!live && !force) || layoutKey === laidOutKey)
            return
        laidOutKey = layoutKey
        layout = App.dayLayout(rangeStart, days, maximumLanes)
    }

    onLayoutKeyChanged: refresh()
    readonly property int stripLanes: Math.max(1, Math.min(layout.lanes, maximumLanes))
    readonly property real stripHeight: stripLanes * lineHeight + 8
    readonly property color lineColor: Qt.alpha(FishUI.Theme.textColor, 0.07)
    // Snapped to this many minutes while creating, moving and resizing.
    readonly property int step: 15

    // The event being moved or resized, and by how much.
    property string dragKey: ""
    property string dragRecurrenceKey: ""
    property int dragDays: 0
    property int dragMinutes: 0
    property int resizeMinutes: 0

    function snap(minutes) {
        return Math.round(minutes / step) * step
    }

    function columnAt(x) {
        return Math.max(0, Math.min(days - 1, Math.floor((x - gutter) / columnWidth)))
    }

    function timeText(minute) {
        const time = new Date(2000, 0, 1, Math.floor(minute / 60), minute % 60)
        return Qt.locale().toString(time, Qt.locale().timeFormat(Locale.ShortFormat))
    }

    // Whole hours without the minutes when the clock has am and pm.
    function hourText(hour) {
        const format = Qt.locale().timeFormat(Locale.ShortFormat)
        return Qt.locale().toString(new Date(2000, 0, 1, hour, 0), App.use24Hour ? format : format.replace(/[:.]mm/, ""))
    }

    function scrollToWorkingHours() {
        const now = new Date()
        const today = rangeStart <= App.today && App.today < Dates.addDays(rangeStart, days)
        const hour = today ? Math.max(0, now.getHours() - 1) : 8
        // A little above the hour, so its label is not cut by the strip.
        grid.contentY = Math.max(0, Math.min(grid.contentHeight - grid.height, hour * hourHeight - 6))
    }

    // Laid out once while preloaded, so the first switch to it is quick too.
    Component.onCompleted: {
        refresh(true)
        Qt.callLater(scrollToWorkingHours)
    }
    onRangeStartChanged: Qt.callLater(scrollToWorkingHours)
    onLiveChanged: {
        refresh()
        if (live)
            Qt.callLater(scrollToWorkingHours)
    }

    Repeater {
        model: view.days === 1 ? 0 : view.days

        Item {
            id: dayHeader

            readonly property date date: Dates.addDays(view.rangeStart, index)
            readonly property bool today: Dates.sameDay(date, App.today)

            x: view.gutter + index * view.columnWidth
            width: view.columnWidth
            height: view.headerHeight

            Row {
                anchors.centerIn: parent
                spacing: 6

                FishUI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: view.days === 1 ? Qt.locale().standaloneDayName(dayHeader.date.getDay(), Locale.LongFormat)
                                          : Qt.locale().standaloneDayName(dayHeader.date.getDay(), Locale.ShortFormat)
                    font.pointSize: 9
                    color: dayHeader.today ? FishUI.Theme.highlightColor : FishUI.Theme.disabledTextColor
                }

                FishUI.RoundedRectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(24, number.implicitWidth + 10)
                    height: 24
                    radius: height / 2
                    continuousCorners: false
                    color: dayHeader.today ? FishUI.Theme.highlightColor : "transparent"

                    FishUI.Label {
                        id: number
                        anchors.centerIn: parent
                        text: dayHeader.date.getDate()
                        font.pointSize: 11
                        font.weight: dayHeader.today ? Font.DemiBold : Font.Normal
                        color: dayHeader.today ? FishUI.Theme.highlightedTextColor : FishUI.Theme.textColor
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: view.window.currentDate = dayHeader.date
                onDoubleClicked: view.window.showDay(dayHeader.date)
            }
        }
    }

    FishUI.Label {
        x: 0
        y: view.headerHeight
        width: view.gutter - 8
        height: view.lineHeight + 8
        horizontalAlignment: Text.AlignRight
        verticalAlignment: Text.AlignVCenter
        text: qsTr("all-day")
        font.pointSize: 8
        color: FishUI.Theme.disabledTextColor
    }

    Item {
        id: strip
        x: 0
        y: view.headerHeight
        width: view.width
        height: view.stripHeight

        Rectangle {
            width: parent.width
            height: 1 / FishUI.Dpi.ratio
            color: view.lineColor
        }

        Repeater {
            model: view.days

            Rectangle {
                x: view.gutter + index * view.columnWidth
                width: 1 / FishUI.Dpi.ratio
                height: strip.height
                color: view.lineColor
            }
        }

        MouseArea {
            x: view.gutter
            width: parent.width - view.gutter
            height: parent.height
            onPressed: view.window.selectEvent(null)
            onDoubleClicked: (mouse) => {
                const date = Dates.addDays(view.rangeStart, view.columnAt(mouse.x + view.gutter))
                view.window.newEvent(date, date, true)
            }
        }

        Repeater {
            model: view.layout.bars

            EventLine {
                required property var modelData

                item: modelData
                window: view.window
                visible: modelData.lane < view.stripLanes
                x: view.gutter + modelData.column * view.columnWidth + (modelData.continuesBefore ? 0 : 3)
                y: 4 + modelData.lane * view.lineHeight
                width: modelData.span * view.columnWidth - (modelData.continuesBefore ? 0 : 3)
                       - (modelData.continuesAfter ? 0 : 3)
                height: view.lineHeight - 2

                onDragFinished: (start, point) => {
                    const days = view.columnAt(mapToItem(view, point.x, point.y).x)
                            - view.columnAt(mapToItem(view, start.x, start.y).x)
                    if (days !== 0)
                        view.window.moveEvent(modelData, days, 0, days, 0)
                }
            }
        }

        Repeater {
            model: view.layout.more

            FishUI.Label {
                required property var modelData

                x: view.gutter + modelData.column * view.columnWidth + 8
                y: 4 + (view.stripLanes - 1) * view.lineHeight
                width: view.columnWidth - 16
                height: view.lineHeight - 2
                verticalAlignment: Text.AlignVCenter
                text: qsTr("%1 more").arg(modelData.count)
                font.pointSize: 8
                font.weight: Font.Medium
                color: stripMore.containsMouse ? FishUI.Theme.highlightColor : FishUI.Theme.disabledTextColor

                MouseArea {
                    id: stripMore
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: view.window.showDay(modelData.date)
                }
            }
        }
    }

    Rectangle {
        y: strip.y + strip.height
        width: view.width
        height: 1 / FishUI.Dpi.ratio
        color: view.lineColor
    }

    Flickable {
        id: grid
        objectName: "hourGrid"
        x: 0
        y: strip.y + strip.height
        width: view.width
        height: view.height - y
        contentWidth: width
        contentHeight: 24 * view.hourHeight + 12
        interactive: false
        clip: true

        FishUI.WheelHandler {
            target: grid
        }
        FishUI.ScrollBar.vertical: FishUI.ScrollBar {}

        Item {
            id: canvas
            width: grid.width
            height: grid.contentHeight

            // The first hour line would double the strip's border.
            Repeater {
                model: 24

                Item {
                    y: 6 + index * view.hourHeight
                    width: canvas.width
                    height: view.hourHeight

                    Rectangle {
                        x: view.gutter - 6
                        width: parent.width - x
                        height: 1 / FishUI.Dpi.ratio
                        color: view.lineColor
                        visible: index > 0
                    }

                    FishUI.Label {
                        x: 0
                        y: -height / 2
                        width: view.gutter - 10
                        horizontalAlignment: Text.AlignRight
                        text: view.hourText(index)
                        font.pointSize: 8
                        color: FishUI.Theme.disabledTextColor
                        visible: index > 0
                    }
                }
            }

            Repeater {
                model: view.days

                Rectangle {
                    x: view.gutter + index * view.columnWidth
                    width: 1 / FishUI.Dpi.ratio
                    height: canvas.height
                    color: view.lineColor
                }
            }

            // Pressing on empty time and dragging marks the new event's time.
            MouseArea {
                id: creator
                objectName: "hourGridArea"
                x: view.gutter
                y: 6
                width: canvas.width - view.gutter
                height: 24 * view.hourHeight
                preventStealing: true

                property bool creating: false
                property int column: 0
                property int anchorMinute: 0
                property int currentMinute: 0
                readonly property int fromMinute: Math.min(anchorMinute, currentMinute)
                readonly property int toMinute: Math.max(anchorMinute, currentMinute)

                function minuteAt(y) {
                    return Math.max(0, Math.min(24 * 60, y / view.hourHeight * 60))
                }

                onPressed: (mouse) => {
                    view.window.selectEvent(null)
                    view.window.focusView()
                    column = view.columnAt(mouse.x + view.gutter)
                    anchorMinute = Math.floor(minuteAt(mouse.y) / view.step) * view.step
                    currentMinute = anchorMinute
                    creating = false
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const minute = view.snap(minuteAt(mouse.y))
                    if (!creating && Math.abs(minute - anchorMinute) >= view.step)
                        creating = true
                    if (creating)
                        currentMinute = minute
                }
                onReleased: {
                    if (!creating)
                        return
                    creating = false
                    const date = Dates.addDays(view.rangeStart, column)
                    if (toMinute > fromMinute)
                        view.window.newEvent(Dates.atMinute(date, fromMinute), Dates.atMinute(date, toMinute), false)
                }
                onDoubleClicked: (mouse) => {
                    const date = Dates.addDays(view.rangeStart, view.columnAt(mouse.x + view.gutter))
                    const start = Math.min(23 * 60, Math.floor(minuteAt(mouse.y) / 30) * 30)
                    view.window.newEvent(Dates.atMinute(date, start), Dates.atMinute(date, start + 60), false)
                }
            }

            FishUI.RoundedRectangle {
                objectName: "newEventPreview"
                x: view.gutter + creator.column * view.columnWidth + 2
                y: 6 + creator.fromMinute / 60 * view.hourHeight
                width: view.columnWidth - 4
                height: (creator.toMinute - creator.fromMinute) / 60 * view.hourHeight
                continuousCorners: false
                radius: 4
                color: Qt.alpha(FishUI.Theme.highlightColor, 0.25)
                border.width: 1
                border.color: FishUI.Theme.highlightColor
                visible: creator.creating

                FishUI.Label {
                    x: 6
                    y: 3
                    text: view.timeText(creator.fromMinute) + " – " + view.timeText(creator.toMinute)
                    font.pointSize: 8
                    color: FishUI.Theme.textColor
                }
            }

            Repeater {
                model: view.layout.blocks

                EventBlock {
                    required property var modelData

                    item: modelData
                    window: view.window
                    gridView: view
                    readonly property bool moving: view.dragKey === modelData.key
                                                   && view.dragRecurrenceKey === modelData.recurrenceKey
                    readonly property real slotWidth: (view.columnWidth - 4) / modelData.slots
                    readonly property int shownStart: modelData.startMinute + (moving ? view.dragMinutes : 0)
                    readonly property int shownEnd: modelData.endMinute + (moving ? view.dragMinutes + view.resizeMinutes : 0)

                    x: view.gutter + (modelData.column + (moving ? view.dragDays : 0)) * view.columnWidth + 2
                       + modelData.slot * slotWidth
                    y: 6 + shownStart / 60 * view.hourHeight + 1
                    width: modelData.span * slotWidth - 2
                    height: Math.max(view.lineHeight, (shownEnd - shownStart) / 60 * view.hourHeight - 2)
                    z: moving ? 2 : 1
                    opacity: moving ? 0.85 : 1
                }
            }

            Item {
                readonly property int column: Dates.daysBetween(view.rangeStart, App.today)
                property int minute: Dates.minuteOf(new Date())

                x: view.gutter + column * view.columnWidth - 4
                y: 6 + minute / 60 * view.hourHeight - 4
                width: view.columnWidth + 4
                height: 8
                z: 3
                visible: column >= 0 && column < view.days

                Timer {
                    interval: 30 * 1000
                    running: parent.visible && view.live
                    repeat: true
                    onTriggered: parent.minute = Dates.minuteOf(new Date())
                }

                FishUI.RoundedRectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    continuousCorners: false
                    color: FishUI.Theme.redColor
                }

                Rectangle {
                    x: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 4
                    height: 1.5
                    color: FishUI.Theme.redColor
                }
            }
        }
    }

    function finishDrag(item) {
        const days = dragDays
        const minutes = dragMinutes
        const resize = resizeMinutes
        dragKey = ""
        dragRecurrenceKey = ""
        dragDays = 0
        dragMinutes = 0
        resizeMinutes = 0
        if (days !== 0 || minutes !== 0 || resize !== 0)
            window.moveEvent(item, days, minutes, days, minutes + resize)
    }
}
