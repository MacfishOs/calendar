// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI
import "Dates.js" as Dates

// A month to pick a day from, dropped down from a date field the way a
// combo box drops its list.
FishUI.MenuPopupWindow {
    id: popup

    property var selectedDate: new Date()
    property date month: Dates.monthStart(selectedDate)

    signal picked(date date)

    popupContentItem: content

    function openFor(item) {
        month = Dates.monthStart(selectedDate)
        showDropDown(item, FishUI.Units.smallSpacing / 2)
    }

    // The window takes its size from the implicit size; a plain size maps it empty.
    Item {
        id: content
        implicitWidth: 252
        implicitHeight: column.implicitHeight + 24
        width: implicitWidth
        height: implicitHeight

        FishUI.BubbleBackground {
            anchors.fill: parent
            view: popup
            color: Qt.rgba(FishUI.Theme.secondBackgroundColor.r, FishUI.Theme.secondBackgroundColor.g,
                           FishUI.Theme.secondBackgroundColor.b, blurAvailable ? 0.8 : 1)
            radius: FishUI.Theme.windowRadius
        }

        ColumnLayout {
            id: column
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
                    text: Qt.locale().standaloneMonthName(popup.month.getMonth()) + " " + popup.month.getFullYear()
                    font.pointSize: 9
                    font.weight: Font.Medium
                }

                FishUI.RoundImageButton {
                    size: 24
                    iconSize: 16
                    iconName: "chevron-left"
                    onClicked: popup.month = Dates.addMonths(popup.month, -1)
                    Accessible.name: qsTr("Previous Month")
                }

                FishUI.RoundImageButton {
                    size: 24
                    iconSize: 16
                    iconName: "chevron-right"
                    onClicked: popup.month = Dates.addMonths(popup.month, 1)
                    Accessible.name: qsTr("Next Month")
                }
            }

            MiniMonth {
                Layout.fillWidth: true
                month: popup.month
                selectedDate: popup.selectedDate
                cellHeight: 28
                onDayClicked: (date) => {
                    popup.picked(date)
                    popup.dismissPopup()
                }
            }
        }
    }
}
