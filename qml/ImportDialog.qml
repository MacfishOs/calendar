// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI

FishUI.Window {
    id: control
    objectName: "importDialog"

    property var files: []

    signal importRequested(var files, string calendarId)

    title: qsTr("Import Events")
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

    readonly property int dialogWidth: 380
    readonly property int dialogHeight: layout.implicitHeight + 16 + 12
    readonly property int textWidth: dialogWidth - 32 - 32

    width: dialogWidth
    height: dialogHeight
    minimumWidth: dialogWidth
    maximumWidth: dialogWidth
    minimumHeight: dialogHeight
    maximumHeight: dialogHeight

    function open() {
        calendarBox.currentIndex = Math.max(0, App.calendars.findIndex(calendar => calendar.id === App.defaultCalendar))
        show()
        raise()
        requestActivate()
    }

    function accept() {
        close()
        importRequested(files, App.calendars[calendarBox.currentIndex].id)
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

    Shortcut {
        sequences: ["Return", "Enter"]
        onActivated: control.accept()
    }

    ColumnLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 16

        Column {
            Layout.fillWidth: true
            Layout.rightMargin: 32
            spacing: 4

            FishUI.Label {
                width: control.textWidth
                text: control.title
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            FishUI.Label {
                width: control.textWidth
                text: control.files.length === 1 ? decodeURIComponent(String(control.files[0]).split("/").pop())
                                                 : qsTr("%1 files").arg(control.files.length)
                textFormat: Text.PlainText
                font.pixelSize: 14
                color: FishUI.Theme.disabledTextColor
                elide: Text.ElideMiddle
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            FishUI.Label {
                text: qsTr("Add to")
            }

            FishUI.ComboBox {
                id: calendarBox
                objectName: "importCalendarBox"
                Layout.fillWidth: true
                model: App.calendars.map(calendar => calendar.name)
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
                objectName: "importButton"
                text: qsTr("Import")
                primary: true
                onClicked: control.accept()
            }
        }
    }
}
