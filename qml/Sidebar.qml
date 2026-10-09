// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FishUI as FishUI
import "Dates.js" as Dates

// The month at a glance and the calendars, each shown or hidden.
Flickable {
    id: sidebar
    objectName: "sidebar"

    required property var window

    topMargin: 4
    bottomMargin: 16
    leftMargin: 12
    rightMargin: 12
    contentWidth: width - (leftMargin + rightMargin)
    contentHeight: content.height
    clip: true

    // Flickable does not include topMargin in its initial content position.
    Component.onCompleted: contentY = -topMargin

    FishUI.WheelHandler {
        target: sidebar
    }
    FishUI.ScrollBar.vertical: FishUI.ScrollBar {}

    // The month the small calendar shows; it follows the main view until
    // its own arrows move it.
    property date shownMonth: Dates.monthStart(window.currentDate)

    Connections {
        target: sidebar.window
        function onCurrentDateChanged() {
            sidebar.shownMonth = Dates.monthStart(sidebar.window.currentDate)
        }
    }

    Column {
        id: content
        width: sidebar.contentWidth
        spacing: 12

        FishUI.RoundedRectangle {
            objectName: "miniMonth"
            width: parent.width
            height: miniColumn.implicitHeight + 24
            radius: FishUI.Theme.mediumRadius
            color: FishUI.Theme.secondBackgroundColor

            ColumnLayout {
                id: miniColumn
                anchors.fill: parent
                anchors.margins: 12
                anchors.topMargin: 10
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    FishUI.Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: 4
                        text: Qt.locale().standaloneMonthName(sidebar.shownMonth.getMonth()) + " "
                              + sidebar.shownMonth.getFullYear()
                        font.pointSize: 9
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    FishUI.RoundImageButton {
                        objectName: "miniPrevious"
                        size: 24
                        iconSize: 16
                        iconName: "chevron-left"
                        onClicked: sidebar.shownMonth = Dates.addMonths(sidebar.shownMonth, -1)
                        Accessible.name: qsTr("Previous Month")
                    }

                    FishUI.RoundImageButton {
                        objectName: "miniNext"
                        size: 24
                        iconSize: 16
                        iconName: "chevron-right"
                        onClicked: sidebar.shownMonth = Dates.addMonths(sidebar.shownMonth, 1)
                        Accessible.name: qsTr("Next Month")
                    }
                }

                MiniMonth {
                    Layout.fillWidth: true
                    month: sidebar.shownMonth
                    selectedDate: sidebar.window.currentDate
                    rangeStart: sidebar.window.view === "week" ? sidebar.window.rangeStart : null
                    rangeEnd: sidebar.window.view === "week" ? Dates.addDays(sidebar.window.rangeStart, 6) : null
                    onDayClicked: (date) => sidebar.window.currentDate = date
                    onDayDoubleClicked: (date) => sidebar.window.showDay(date)
                }
            }
        }

        RowLayout {
            width: parent.width
            spacing: 0

            FishUI.Label {
                Layout.fillWidth: true
                leftPadding: 4
                text: qsTr("Calendars")
                color: FishUI.Theme.disabledTextColor
                font.pointSize: 9
                font.weight: Font.Medium
            }

            FishUI.RoundImageButton {
                objectName: "addCalendar"
                size: 24
                iconSize: 16
                source: "qrc:/icons/add.svg"
                image.color: FishUI.Theme.textColor
                onClicked: sidebar.window.newCalendar()
                Accessible.name: qsTr("New Calendar")
            }
        }

        FishUI.RoundedRectangle {
            objectName: "calendarList"
            width: parent.width
            height: calendarColumn.implicitHeight + 12
            radius: FishUI.Theme.mediumRadius
            color: FishUI.Theme.secondBackgroundColor

            Column {
                id: calendarColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 6

                Repeater {
                    model: App.calendars

                    ItemDelegate {
                        id: row

                        required property var modelData

                        width: calendarColumn.width
                        height: 34
                        padding: 0
                        leftPadding: 10
                        rightPadding: 10
                        hoverEnabled: true
                        onClicked: App.setCalendarVisible(modelData.id, !modelData.visible)
                        Accessible.name: modelData.name
                        Accessible.role: Accessible.CheckBox
                        Accessible.checked: modelData.visible

                        // Inset 6 into the radius 10 card.
                        background: FishUI.RoundedRectangle {
                            continuousCorners: false
                            radius: FishUI.Theme.mediumRadius - 6
                            color: row.down ? Qt.alpha(FishUI.Theme.textColor, 0.08)
                                 : row.hovered ? Qt.alpha(FishUI.Theme.textColor, 0.04) : "transparent"
                        }

                        contentItem: RowLayout {
                            spacing: 10

                            FishUI.RoundedRectangle {
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                continuousCorners: false
                                radius: 5
                                color: row.modelData.visible ? row.modelData.color : "transparent"
                                border.width: row.modelData.visible ? 0 : 1.5
                                border.color: row.modelData.color

                                FishUI.Icon {
                                    anchors.centerIn: parent
                                    width: 12
                                    height: 12
                                    name: "check"
                                    color: "white"
                                    visible: row.modelData.visible
                                }
                            }

                            FishUI.Label {
                                Layout.fillWidth: true
                                text: row.modelData.name
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                            }
                        }

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: sidebar.window.showCalendarMenu(row.modelData)
                        }
                    }
                }
            }
        }
    }
}
