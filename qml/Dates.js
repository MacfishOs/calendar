// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library

// Dates are local midnights; adding days through the constructor keeps them
// at midnight across daylight saving changes.

function day(date) {
    return new Date(date.getFullYear(), date.getMonth(), date.getDate())
}

function addDays(date, days) {
    return new Date(date.getFullYear(), date.getMonth(), date.getDate() + days)
}

// Same day of the month, or the month's last day when it has fewer.
function addMonths(date, months) {
    const first = new Date(date.getFullYear(), date.getMonth() + months, 1)
    const last = new Date(first.getFullYear(), first.getMonth() + 1, 0).getDate()
    return new Date(first.getFullYear(), first.getMonth(), Math.min(date.getDate(), last))
}

function sameDay(a, b) {
    return !!a && !!b && a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth()
            && a.getDate() === b.getDate()
}

function sameMonth(a, b) {
    return !!a && !!b && a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth()
}

function daysBetween(from, to) {
    return Math.round((day(to) - day(from)) / 86400000)
}

// firstDay is 1 for Monday to 7 for Sunday.
function weekStart(date, firstDay) {
    const offset = (date.getDay() - firstDay % 7 + 7) % 7
    return addDays(date, -offset)
}

function monthStart(date) {
    return new Date(date.getFullYear(), date.getMonth(), 1)
}

// The first day of the six-week grid that shows the month.
function monthGridStart(date, firstDay) {
    return weekStart(monthStart(date), firstDay)
}

// Minutes since midnight to a date on that day.
function atMinute(date, minute) {
    return new Date(date.getFullYear(), date.getMonth(), date.getDate(), Math.floor(minute / 60), minute % 60)
}

function minuteOf(date) {
    return date.getHours() * 60 + date.getMinutes()
}

// Weekday index 0-6 of the column, counted from the first day of the week.
function weekdayOfColumn(column, firstDay) {
    return (firstDay % 7 + column) % 7
}

// The day as the language writes it in full, without a time. A script
// library cannot see the Locale enums: 0 is Locale.LongFormat.
function longDate(date) {
    return Qt.locale().toString(date, Qt.locale().dateFormat(0))
}
