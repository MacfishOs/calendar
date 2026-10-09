// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import FishUI as FishUI
import "Dates.js" as Dates

FishUI.Window {
    id: control
    objectName: "eventDialog"

    // From App.eventData or App.newEventData.
    required property var event
    readonly property bool isNew: event.key.length === 0

    signal saveRequested(var data)
    signal deleteRequested()

    title: isNew ? qsTr("New Event") : qsTr("Edit Event")

    readonly property int dialogWidth: 480
    readonly property int dialogHeight: mainLayout.implicitHeight + header.height + 24

    width: dialogWidth
    height: dialogHeight
    minimumWidth: dialogWidth
    maximumWidth: dialogWidth
    minimumHeight: dialogHeight
    maximumHeight: dialogHeight

    flags: Qt.Dialog | Qt.FramelessWindowHint
    modality: Qt.WindowModal
    visible: false
    minimizeButtonVisible: false
    header.height: 44
    windowButtonsAlignment: Qt.AlignVCenter
    windowButtonsTopMargin: 0
    windowButtonsRightMargin: FishUI.Units.smallSpacing
    restorationBehavior: FishUI.Window.Disabled
    background.color: FishUI.Theme.backgroundColor
    color: background.color

    readonly property color separatorColor: Qt.alpha(FishUI.Theme.textColor, 0.08)
    readonly property var times: {
        const list = []
        for (let minute = 0; minute < 24 * 60; minute += 15)
            list.push(timeText(minute))
        return list
    }

    // As the views write times, with the language's own am and pm.
    function timeText(minute) {
        const time = new Date(2000, 0, 1, Math.floor(minute / 60), minute % 60)
        return Qt.locale().toString(time, Qt.locale().timeFormat(Locale.ShortFormat))
    }

    property bool allDay: event.allDay
    property date startDate: Dates.day(event.start)
    property date endDate: Dates.day(event.end)
    property int startMinute: event.allDay ? 9 * 60 : Dates.minuteOf(event.start)
    property int endMinute: event.allDay ? 10 * 60 : Dates.minuteOf(event.end)
    property int repeat: event.repeat
    property int repeatEnd: event.repeatEnd
    property date repeatUntil: Dates.day(event.repeatUntil)
    property int repeatCount: event.repeatCount

    readonly property date start: allDay ? startDate : Dates.atMinute(startDate, startMinute)
    readonly property date end: allDay ? endDate : Dates.atMinute(endDate, endMinute)
    readonly property bool valid: end >= start

    // Repeat choices; Custom only for a rule made elsewhere.
    readonly property var repeatChoices: {
        const list = [
            { text: qsTr("Never"), value: 0 },
            { text: qsTr("Every Day"), value: 1 },
            { text: qsTr("Every Weekday"), value: 2 },
            { text: qsTr("Every Week"), value: 3 },
            { text: qsTr("Every Month"), value: 4 },
            { text: qsTr("Every Year"), value: 5 }
        ]
        if (event.repeat === 6)
            list.push({ text: qsTr("Custom"), value: 6 })
        return list
    }

    // Alarm choices, in minutes from the start; all-day ones count from midnight.
    readonly property var alarmChoices: {
        const list = [{ text: qsTr("None"), alarm: 0, offset: 0 }]
        if (allDay) {
            list.push({ text: qsTr("On the Day (9:00)"), alarm: 1, offset: 9 * 60 })
            list.push({ text: qsTr("1 Day Before (9:00)"), alarm: 1, offset: -15 * 60 })
            list.push({ text: qsTr("2 Days Before (9:00)"), alarm: 1, offset: -(24 + 15) * 60 })
            list.push({ text: qsTr("1 Week Before (9:00)"), alarm: 1, offset: -(6 * 24 + 15) * 60 })
        } else {
            list.push({ text: qsTr("At Time of Event"), alarm: 1, offset: 0 })
            list.push({ text: qsTr("5 Minutes Before"), alarm: 1, offset: -5 })
            list.push({ text: qsTr("10 Minutes Before"), alarm: 1, offset: -10 })
            list.push({ text: qsTr("15 Minutes Before"), alarm: 1, offset: -15 })
            list.push({ text: qsTr("30 Minutes Before"), alarm: 1, offset: -30 })
            list.push({ text: qsTr("1 Hour Before"), alarm: 1, offset: -60 })
            list.push({ text: qsTr("2 Hours Before"), alarm: 1, offset: -120 })
            list.push({ text: qsTr("1 Day Before"), alarm: 1, offset: -24 * 60 })
            list.push({ text: qsTr("2 Days Before"), alarm: 1, offset: -48 * 60 })
            list.push({ text: qsTr("1 Week Before"), alarm: 1, offset: -7 * 24 * 60 })
        }
        const kept = list.some(choice => choice.alarm === event.alarm && choice.offset === event.alarmOffset)
        if (event.alarm === 2 || (event.alarm === 1 && event.allDay === allDay && !kept))
            list.push({ text: qsTr("Custom"), alarm: event.alarm, offset: event.alarmOffset })
        return list
    }
    property int alarmIndex: 0

    function alarmIndexFor(alarm, offset) {
        for (let i = 0; i < alarmChoices.length; ++i) {
            if (alarmChoices[i].alarm === alarm && alarmChoices[i].offset === offset)
                return i
        }
        return 0
    }

    // Typed times: "9", "930", "9:30", "21:30", "9:30 pm", "9pm".
    function parseTime(text, fallback) {
        const match = text.trim().toLowerCase().match(/^(\d{1,2})(?:[:.h]?(\d{2}))?\s*(a|p|am|pm|上午|下午)?$/)
        if (!match)
            return fallback
        let hour = parseInt(match[1])
        const minute = match[2] ? parseInt(match[2]) : 0
        const meridiem = match[3] || ""
        if (meridiem.startsWith("p") || meridiem === "下午") {
            if (hour < 12)
                hour += 12
        } else if ((meridiem.startsWith("a") || meridiem === "上午") && hour === 12) {
            hour = 0
        }
        if (hour > 23 || minute > 59)
            return fallback
        return hour * 60 + minute
    }

    function setStart(date, minute) {
        // The event keeps its length when its start moves.
        const length = end - start
        startDate = Dates.day(date)
        startMinute = minute
        const moved = new Date(start.getTime() + length)
        endDate = Dates.day(moved)
        endMinute = Dates.minuteOf(moved)
    }

    function save() {
        if (!valid)
            return
        const data = Object.assign({}, event)
        data.title = titleField.text
        data.location = locationField.text
        data.notes = notesField.text
        data.allDay = allDay
        data.start = start
        data.end = end
        data.repeat = repeat
        data.repeatEnd = repeatEnd
        data.repeatUntil = repeatUntil
        data.repeatCount = repeatCount
        data.alarm = alarmChoices[alarmIndex].alarm
        data.alarmOffset = alarmChoices[alarmIndex].offset
        data.calendarId = App.calendars[calendarBox.currentIndex].id
        saveRequested(data)
    }

    Component.onCompleted: alarmIndex = alarmIndexFor(event.alarm, event.alarmOffset)
    onAllDayChanged: alarmIndex = allDay === event.allDay ? alarmIndexFor(event.alarm, event.alarmOffset) : 0
    onVisibleChanged: {
        if (visible)
            titleField.forceActiveFocus()
    }

    // Escape puts away an open date picker first; the picker never has the keyboard.
    Shortcut {
        sequence: "Escape"
        onActivated: {
            for (const button of [startDateButton, endDateButton, untilDateButton]) {
                if (button.popupVisible) {
                    button.dismiss()
                    return
                }
            }
            control.close()
        }
    }

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        onActivated: control.save()
    }

    component Card: FishUI.RoundedRectangle {
        default property alias rows: _rows.data

        Layout.fillWidth: true
        implicitHeight: _rows.implicitHeight
        radius: FishUI.Theme.bigRadius
        color: FishUI.Theme.secondBackgroundColor

        ColumnLayout {
            id: _rows
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 0
        }
    }

    // A label on the left, its controls on the right, a hairline below unless last.
    component FormRow: Item {
        property alias text: _label.text
        property bool last: false
        default property alias content: _content.data

        Layout.fillWidth: true
        implicitHeight: 44
        visible: true

        FishUI.Label {
            id: _label
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
        }

        Row {
            id: _content
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
        }

        FishUI.RoundedRectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            height: 1 / FishUI.Dpi.ratio
            continuousCorners: false
            color: control.separatorColor
            visible: !parent.last
        }
    }

    component PlainField: FishUI.TextField {
        Layout.fillWidth: true
        Layout.preferredHeight: 44
        leftPadding: 14
        rightPadding: 14
        background: Item {}
    }

    component DateButton: AbstractButton {
        id: _dateButton
        property date date
        readonly property bool popupVisible: _datePopup.visible
        signal picked(date date)

        function dismiss() {
            _datePopup.dismissPopup()
        }

        implicitWidth: Math.max(150, _dateLabel.implicitWidth + 24)
        // As tall as the combo boxes beside it.
        implicitHeight: FishUI.Units.iconSizes.medium + FishUI.Units.smallSpacing / 2
        hoverEnabled: true
        Accessible.name: _dateLabel.text

        background: FishUI.RoundedRectangle {
            radius: FishUI.Theme.smallRadius
            color: _dateButton.down ? (FishUI.Theme.darkMode ? Qt.lighter(FishUI.Theme.alternateBackgroundColor, 1.1)
                                                             : Qt.darker(FishUI.Theme.alternateBackgroundColor, 1.2))
                                    : FishUI.Theme.alternateBackgroundColor
            border.width: 1
            border.color: _datePopup.visible || _dateButton.activeFocus ? FishUI.Theme.highlightColor : color
        }

        contentItem: FishUI.Label {
            id: _dateLabel
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: Dates.longDate(_dateButton.date)
        }

        onClicked: {
            _datePopup.selectedDate = date
            _datePopup.openFor(_dateButton)
        }

        DatePopup {
            id: _datePopup
            onPicked: (date) => _dateButton.picked(date)
        }
    }

    component TimeBox: FishUI.ComboBox {
        id: _timeBox
        property int minute
        signal changed(int minute)

        // Typing replaces the text; it shows the time again once read.
        function commit(value) {
            changed(value)
            editText = control.timeText(minute)
        }

        width: 110
        editable: true
        model: control.times
        currentIndex: minute % 15 === 0 ? minute / 15 : -1
        Component.onCompleted: commit(minute)
        onMinuteChanged: commit(minute)
        onActivated: (index) => commit(index * 15)
        onAccepted: commit(control.parseTime(editText, minute))
        onActiveFocusChanged: {
            if (!activeFocus)
                commit(control.parseTime(editText, minute))
        }
        Accessible.name: editText
    }

    headerItem: Item {
        FishUI.Label {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: (control.width - parent.width) / 2
            text: control.title
            font.pixelSize: 13
            font.weight: Font.Medium
        }
    }

    ColumnLayout {
        id: mainLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 12

        Card {
            PlainField {
                id: titleField
                objectName: "titleField"
                text: control.event.title
                placeholderText: qsTr("Title")
                font.pointSize: 11
                font.weight: Font.Medium
                onAccepted: control.save()
            }

            FishUI.RoundedRectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.preferredHeight: 1 / FishUI.Dpi.ratio
                continuousCorners: false
                color: control.separatorColor
            }

            PlainField {
                id: locationField
                objectName: "locationField"
                text: control.event.location
                placeholderText: qsTr("Location")
                onAccepted: control.save()
            }
        }

        Card {
            FormRow {
                text: qsTr("All Day")

                FishUI.Switch {
                    objectName: "allDaySwitch"
                    anchors.verticalCenter: parent.verticalCenter
                    checked: control.allDay
                    onToggled: control.allDay = checked
                }
            }

            FormRow {
                text: qsTr("Starts")

                DateButton {
                    id: startDateButton
                    objectName: "startDate"
                    date: control.startDate
                    onPicked: (date) => control.setStart(date, control.startMinute)
                }

                TimeBox {
                    objectName: "startTime"
                    visible: !control.allDay
                    minute: control.startMinute
                    onChanged: (minute) => control.setStart(control.startDate, minute)
                }
            }

            FormRow {
                text: qsTr("Ends")
                last: true

                DateButton {
                    id: endDateButton
                    objectName: "endDate"
                    date: control.endDate
                    onPicked: (date) => control.endDate = date
                }

                TimeBox {
                    objectName: "endTime"
                    visible: !control.allDay
                    minute: control.endMinute
                    onChanged: (minute) => control.endMinute = minute
                }
            }
        }

        FishUI.Label {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.topMargin: -6
            text: qsTr("The event ends before it starts.")
            font.pointSize: 8
            color: FishUI.Theme.redColor
            visible: !control.valid
        }

        Card {
            FormRow {
                text: qsTr("Repeat")

                FishUI.ComboBox {
                    objectName: "repeatBox"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 200
                    model: control.repeatChoices.map(choice => choice.text)
                    currentIndex: Math.max(0, control.repeatChoices.findIndex(choice => choice.value === control.repeat))
                    onActivated: (index) => control.repeat = control.repeatChoices[index].value
                }
            }

            FormRow {
                text: qsTr("End Repeat")
                visible: control.repeat !== 0 && control.repeat !== 6

                FishUI.ComboBox {
                    objectName: "repeatEndBox"
                    anchors.verticalCenter: parent.verticalCenter
                    width: control.repeatEnd === 0 ? 200 : 110
                    model: [qsTr("Never"), qsTr("On Date"), qsTr("After")]
                    currentIndex: control.repeatEnd
                    onActivated: (index) => control.repeatEnd = index
                }

                DateButton {
                    id: untilDateButton
                    visible: control.repeatEnd === 1
                    date: control.repeatUntil
                    onPicked: (date) => control.repeatUntil = date
                }

                FishUI.TextField {
                    objectName: "repeatCountField"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 56
                    visible: control.repeatEnd === 2
                    horizontalAlignment: Text.AlignHCenter
                    text: control.repeatCount
                    validator: IntValidator { bottom: 1; top: 999 }
                    onTextEdited: {
                        if (acceptableInput)
                            control.repeatCount = parseInt(text)
                    }
                }

                FishUI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: control.repeatEnd === 2
                    text: control.repeatCount === 1 ? qsTr("time") : qsTr("times")
                }
            }

            FormRow {
                text: qsTr("Alert")

                FishUI.ComboBox {
                    objectName: "alarmBox"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 200
                    model: control.alarmChoices.map(choice => choice.text)
                    currentIndex: control.alarmIndex
                    onActivated: (index) => control.alarmIndex = index
                }
            }

            FormRow {
                text: qsTr("Calendar")
                last: true

                FishUI.ComboBox {
                    id: calendarBox
                    objectName: "calendarBox"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 200
                    model: App.calendars.map(calendar => calendar.name)
                    currentIndex: Math.max(0, App.calendars.findIndex(calendar => calendar.id === control.event.calendarId))
                }
            }
        }

        Card {
            FishUI.TextArea {
                id: notesField
                objectName: "notesField"
                Layout.fillWidth: true
                Layout.preferredHeight: 84
                leftPadding: 14
                rightPadding: 14
                topPadding: 12
                bottomPadding: 12
                text: control.event.notes
                placeholderText: qsTr("Notes")
                wrapMode: TextEdit.Wrap
                background: Item {}
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8

            FishUI.ConfirmDialogButton {
                objectName: "deleteButton"
                text: qsTr("Delete Event")
                visible: !control.isNew
                onClicked: control.deleteRequested()
            }

            Item {
                Layout.fillWidth: true
            }

            FishUI.ConfirmDialogButton {
                text: qsTr("Cancel")
                onClicked: control.close()
            }

            FishUI.ConfirmDialogButton {
                objectName: "saveButton"
                text: control.isNew ? qsTr("Add") : qsTr("Save")
                primary: true
                enabled: control.valid
                onClicked: control.save()
            }
        }
    }
}
