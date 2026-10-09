// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import FishUI as FishUI
import "Dates.js" as Dates

// The twelve months of the year, days with events marked by a dot.
Item {
    id: view
    objectName: "yearView"

    required property var window
    // Hidden by opacity, not visibility: showing hundreds of items again rebuilds their nodes.
    property bool live: false

    readonly property int year: window.currentDate.getFullYear()
    readonly property real padding: 16
    readonly property real columnSpacing: 16
    readonly property real rowSpacing: 16
    // The whole year always fits: four columns unless a day would get under 15 px.
    readonly property int columns: (width - 2 * padding - 3 * columnSpacing) / 4 >= 7 * 15 ? 4 : 3
    readonly property int rows: 12 / columns
    readonly property real monthWidth: (width - 2 * padding - (columns - 1) * columnSpacing) / columns
    readonly property real monthHeight: (height - 2 * padding - (rows - 1) * rowSpacing) / rows
    readonly property real titleHeight: 24
    readonly property real weekdayHeight: 20
    // The month name and the weekday row keep their size; the day rows take what is left.
    readonly property real cellHeight: Math.min(30, (monthHeight - titleHeight - weekdayHeight) / 6)
    // One lookup for the whole year, from January's grid to the end of December's.
    readonly property date busyStart: Dates.monthGridStart(new Date(year, 0, 1), App.firstDayOfWeek)
    // Looked up again only when shown and the year or the events changed.
    property var busy: []
    readonly property string busyKey: [App.revision, busyStart.getTime()].join()
    property string busyKeyShown: ""

    function refresh(force) {
        if ((!live && !force) || busyKey === busyKeyShown)
            return
        busyKeyShown = busyKey
        const end = Dates.addDays(Dates.monthGridStart(new Date(year, 11, 1), App.firstDayOfWeek), 42)
        busy = App.busyDays(busyStart, Dates.daysBetween(busyStart, end))
    }

    onBusyKeyChanged: refresh()
    onLiveChanged: refresh()
    // Laid out once while preloaded, so the first switch to it is quick too.
    Component.onCompleted: refresh(true)

    Repeater {
        model: 12

        Item {
            id: monthItem

            readonly property date month: new Date(view.year, index, 1)

            x: view.padding + (index % view.columns) * (view.monthWidth + view.columnSpacing)
            y: view.padding + Math.floor(index / view.columns) * (view.monthHeight + view.rowSpacing)
            width: view.monthWidth
            height: view.monthHeight

            FishUI.Label {
                id: title
                x: 4
                width: parent.width - 8
                height: view.titleHeight
                verticalAlignment: Text.AlignVCenter
                text: Qt.locale().standaloneMonthName(index)
                font.pointSize: 10
                font.weight: Font.DemiBold
                color: FishUI.Theme.highlightColor
                opacity: titleArea.containsMouse ? 0.75 : 1
                elide: Text.ElideRight

                MouseArea {
                    id: titleArea
                    width: Math.min(parent.width, parent.implicitWidth)
                    height: parent.height
                    hoverEnabled: true
                    onClicked: view.window.showMonth(monthItem.month)
                }
            }

            MiniMonth {
                y: view.titleHeight
                width: parent.width
                headerHeight: view.weekdayHeight
                cellHeight: view.cellHeight
                month: monthItem.month
                selectedDate: view.window.currentDate
                fontSize: cellHeight >= 18 && width / 7 >= 19 ? 8 : 7
                busy: {
                    const offset = Dates.daysBetween(view.busyStart, gridStart)
                    return view.busy.slice(offset, offset + 42)
                }
                onDayClicked: (date) => view.window.currentDate = date
                onDayDoubleClicked: (date) => view.window.showDay(date)
            }
        }
    }
}
