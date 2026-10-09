// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import FishUI as FishUI

FishUI.DialogWindow {
    id: control
    objectName: "settingsDialog"

    title: qsTr("Settings")

    readonly property int dialogWidth: 420
    contentHeight: mainLayout.implicitHeight + contentPadding

    width: dialogWidth

    background.color: FishUI.Theme.backgroundColor

    readonly property color separatorColor: Qt.alpha(FishUI.Theme.textColor, 0.08)
    // 0 follows the language.
    readonly property var weekStarts: [0, 7, 1, 6]

    Shortcut {
        sequence: "Escape"
        onActivated: control.close()
    }

    ColumnLayout {
        id: mainLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: control.contentPadding
        anchors.rightMargin: control.contentPadding
        spacing: FishUI.Units.largeSpacing

        FishUI.RoundedRectangle {
            Layout.fillWidth: true
            implicitHeight: rows.implicitHeight
            radius: FishUI.Theme.bigRadius
            color: FishUI.Theme.secondBackgroundColor

            ColumnLayout {
                id: rows
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    implicitHeight: 44

                    FishUI.Label {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Week Starts On")
                    }

                    FishUI.ComboBox {
                        objectName: "weekStartBox"
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        width: 180
                        model: control.weekStarts.map(day => day === 0 ? qsTr("System Default")
                                                                       : Qt.locale().standaloneDayName(day % 7, Locale.LongFormat))
                        currentIndex: Math.max(0, control.weekStarts.indexOf(App.weekStart))
                        onActivated: (index) => App.weekStart = control.weekStarts[index]
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
                    }
                }

                Item {
                    Layout.fillWidth: true
                    implicitHeight: 44

                    FishUI.Label {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Show Week Numbers")
                    }

                    FishUI.Switch {
                        objectName: "weekNumbersSwitch"
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        checked: App.showWeekNumbers
                        onToggled: App.showWeekNumbers = checked
                    }
                }
            }
        }
    }
}
