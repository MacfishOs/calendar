// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import FishUI as FishUI
import "Dates.js" as Dates

// Weekday initials over six weeks of days, as the sidebar, the date picker
// and the year view show a month.
Item {
    id: control

    property date month: new Date()
    property var selectedDate: null
    // A run of days the main view shows, tinted behind the numbers.
    property var rangeStart: null
    property var rangeEnd: null
    // Days of the neighbouring months: shown dimmed, or left out.
    property bool showOutsideDays: true
    // One entry per day of the grid; days with events get a dot.
    property var busy: []
    property real cellHeight: 26
    property real fontSize: 8

    readonly property int firstDay: App.firstDayOfWeek
    readonly property date gridStart: Dates.monthGridStart(month, firstDay)
    readonly property real cellWidth: width / 7
    property real headerHeight: cellHeight
    readonly property int monthFirst: Dates.daysBetween(gridStart, Dates.monthStart(month))
    readonly property int monthLast: monthFirst + new Date(month.getFullYear(), month.getMonth() + 1, 0).getDate() - 1
    readonly property int todayIndex: indexOf(App.today)
    readonly property int selectedIndex: indexOf(selectedDate)
    // Packed rows let the plate reach a little into the rows around it.
    readonly property real plateSize: Math.min(cellWidth - 2, cellHeight + 4, 22)

    signal dayClicked(date date)
    signal dayDoubleClicked(date date)

    implicitWidth: 7 * 28
    implicitHeight: headerHeight + 6 * cellHeight

    function indexOf(date) {
        if (!date)
            return -1
        const index = Dates.daysBetween(gridStart, date)
        return index >= 0 && index < 42 ? index : -1
    }

    function cellX(index) {
        return (index % 7) * cellWidth
    }

    function cellY(index) {
        return headerHeight + Math.floor(index / 7) * cellHeight
    }

    function shown(index) {
        return showOutsideDays || inMonth(index)
    }

    // Plates and dots mark the month's own days; a neighbour's dimmed copy of a day gets none.
    function inMonth(index) {
        return index >= monthFirst && index <= monthLast
    }

    Row {
        Repeater {
            model: 7

            Text {
                width: control.cellWidth
                height: control.headerHeight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                renderType: FishUI.Theme.renderType
                text: Qt.locale().standaloneDayName(Dates.weekdayOfColumn(index, control.firstDay), Locale.NarrowFormat)
                font.pointSize: control.fontSize
                color: FishUI.Theme.disabledTextColor
            }
        }
    }

    // The shown range as one band per row, round at its ends.
    Repeater {
        model: {
            const first = control.indexOf(control.rangeStart)
            const last = control.indexOf(control.rangeEnd)
            if (first < 0 || last <= first)
                return []
            const rows = []
            for (let start = first; start <= last; start = (Math.floor(start / 7) + 1) * 7)
                rows.push({ first: start, last: Math.min(last, Math.floor(start / 7) * 7 + 6) })
            return rows
        }

        Rectangle {
            required property var modelData

            x: control.cellX(modelData.first) + 2
            y: control.cellY(modelData.first) + (control.cellHeight - height) / 2
            width: (modelData.last - modelData.first + 1) * control.cellWidth - 4
            height: control.plateSize
            radius: height / 2
            color: Qt.alpha(FishUI.Theme.highlightColor, FishUI.Theme.darkMode ? 0.15 : 0.09)
        }
    }

    FishUI.RoundedRectangle {
        x: control.cellX(control.selectedIndex) + (control.cellWidth - width) / 2
        y: control.cellY(control.selectedIndex) + (control.cellHeight - height) / 2
        width: control.plateSize
        height: width
        radius: width / 2
        continuousCorners: false
        color: Qt.alpha(FishUI.Theme.highlightColor, FishUI.Theme.darkMode ? 0.25 : 0.15)
        visible: control.selectedIndex !== control.todayIndex && control.inMonth(control.selectedIndex)
                 && control.rangeStart === null
    }

    FishUI.RoundedRectangle {
        x: control.cellX(control.todayIndex) + (control.cellWidth - width) / 2
        y: control.cellY(control.todayIndex) + (control.cellHeight - height) / 2
        width: control.plateSize
        height: width
        radius: width / 2
        continuousCorners: false
        color: FishUI.Theme.highlightColor
        visible: control.inMonth(control.todayIndex)
    }

    // One plain Text per day, the plates and the mouse area shared: the year
    // view builds twelve of these at once.
    Repeater {
        model: 42

        Text {
            readonly property bool inMonth: control.inMonth(index)
            readonly property bool today: inMonth && index === control.todayIndex
            readonly property bool selected: inMonth && index === control.selectedIndex && control.rangeStart === null

            x: control.cellX(index)
            y: control.cellY(index)
            width: control.cellWidth
            height: control.cellHeight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            renderType: FishUI.Theme.renderType
            visible: control.showOutsideDays || inMonth
            text: new Date(control.gridStart.getFullYear(), control.gridStart.getMonth(),
                           control.gridStart.getDate() + index).getDate()
            font.pointSize: control.fontSize
            font.weight: today || selected ? Font.DemiBold : Font.Normal
            color: today ? FishUI.Theme.highlightedTextColor
                 : selected ? FishUI.Theme.highlightColor
                 : inMonth ? FishUI.Theme.textColor : FishUI.Theme.disabledTextColor
            opacity: inMonth || today ? 1 : 0.6
        }
    }

    Repeater {
        // Dots in cells too short for them would sit on the next row's numbers.
        model: {
            const days = []
            for (let i = control.monthFirst; i <= control.monthLast && control.cellHeight >= 20; ++i) {
                if (control.busy[i])
                    days.push(i)
            }
            return days
        }

        FishUI.RoundedRectangle {
            required property int modelData

            x: control.cellX(modelData) + (control.cellWidth - width) / 2
            y: control.cellY(modelData) + control.cellHeight - height - Math.max(1, (control.cellHeight - 22) / 2 - 1)
            width: 4
            height: 4
            radius: 2
            continuousCorners: false
            color: modelData === control.todayIndex ? FishUI.Theme.highlightedTextColor
                                                    : FishUI.Theme.disabledTextColor
        }
    }

    MouseArea {
        y: control.headerHeight
        width: control.width
        height: control.height - y

        function dateAt(mouse) {
            const index = Math.min(6, Math.floor(mouse.y / control.cellHeight)) * 7
                    + Math.min(6, Math.floor(mouse.x / control.cellWidth))
            return control.shown(index) ? Dates.addDays(control.gridStart, index) : null
        }

        onClicked: (mouse) => {
            const date = dateAt(mouse)
            if (date)
                control.dayClicked(date)
        }
        onDoubleClicked: (mouse) => {
            const date = dateAt(mouse)
            if (date)
                control.dayDoubleClicked(date)
        }
    }
}
