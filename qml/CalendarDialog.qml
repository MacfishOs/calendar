// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FishUI as FishUI

FishUI.Window {
    id: control
    objectName: "calendarDialog"

    // Empty for a new calendar.
    property string calendarId
    property string name
    property color calendarColor: FishUI.Theme.blueColor

    readonly property var colors: [FishUI.Theme.blueColor, FishUI.Theme.purpleColor, FishUI.Theme.pinkColor,
                                   FishUI.Theme.redColor, FishUI.Theme.orangeColor, FishUI.Theme.greenColor,
                                   FishUI.Theme.greyColor]

    title: calendarId.length > 0 ? qsTr("Edit Calendar") : qsTr("New Calendar")
    flags: Qt.Dialog | Qt.FramelessWindowHint
    modality: Qt.WindowModal
    visible: false
    minimizeButtonVisible: false
    contentTopMargin: 0
    windowButtonsTopMargin: 8
    windowButtonsRightMargin: 8
    header.height: 44
    restorationBehavior: FishUI.Window.Disabled
    background.color: FishUI.Theme.secondBackgroundColor
    color: background.color

    readonly property int dialogWidth: 360
    readonly property int dialogHeight: layout.implicitHeight + 16 + 12

    width: dialogWidth
    height: dialogHeight
    minimumWidth: dialogWidth
    maximumWidth: dialogWidth
    minimumHeight: dialogHeight
    maximumHeight: dialogHeight

    function open() {
        show()
        raise()
        requestActivate()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function accept() {
        const name = nameField.text.trim()
        if (name.length === 0)
            return
        if (calendarId.length > 0)
            App.updateCalendar(calendarId, name, calendarColor)
        else
            App.createCalendar(name, calendarColor)
        close()
    }

    DragHandler {
        parent: control.contentItem
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen
        grabPermissions: PointerHandler.CanTakeOverFromItems
                         | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                         | PointerHandler.ApprovesTakeOverByAnything
        onActiveChanged: if (active) control.controller.startSystemMove()
    }

    Shortcut {
        sequence: "Escape"
        onActivated: control.close()
    }

    ColumnLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 16

        FishUI.Label {
            Layout.fillWidth: true
            Layout.rightMargin: 32
            text: control.title
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }

        FishUI.TextField {
            id: nameField
            objectName: "calendarName"
            Layout.fillWidth: true
            text: control.name
            placeholderText: qsTr("Name")
            onAccepted: control.accept()
        }

        Row {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10

            Repeater {
                model: control.colors

                AbstractButton {
                    id: swatch
                    required property color modelData
                    readonly property bool chosen: Qt.colorEqual(modelData, control.calendarColor)

                    width: 28
                    height: 28
                    onClicked: control.calendarColor = modelData
                    Accessible.role: Accessible.RadioButton
                    Accessible.checked: chosen

                    background: FishUI.RoundedRectangle {
                        continuousCorners: false
                        radius: width / 2
                        color: "transparent"
                        border.width: swatch.chosen ? 2 : 0
                        border.color: swatch.modelData

                        FishUI.RoundedRectangle {
                            anchors.fill: parent
                            anchors.margins: swatch.chosen ? 4 : 2
                            continuousCorners: false
                            radius: width / 2
                            color: swatch.modelData
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8

            FishUI.ConfirmDialogButton {
                text: qsTr("Cancel")
                onClicked: control.close()
            }

            FishUI.ConfirmDialogButton {
                objectName: "calendarSave"
                text: control.calendarId.length > 0 ? qsTr("Save") : qsTr("Create")
                primary: true
                enabled: nameField.text.trim().length > 0
                onClicked: control.accept()
            }
        }
    }
}
