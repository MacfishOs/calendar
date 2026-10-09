// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI

// Asks which occurrences of a repeating event a change or a removal is for,
// in FishUI.ConfirmDialog's layout with a third button.
FishUI.Window {
    id: control
    objectName: "scopeDialog"

    property string text
    property string thisText
    property string laterText
    property bool destructive: false
    // Without "only this one", as when the rule itself changed.
    property bool seriesOnly: false

    // EventFile::Scope: 0 this event, 1 this and later ones.
    signal chosen(int scope)

    flags: Qt.Dialog | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
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

    readonly property int dialogWidth: Math.max(360, actions.implicitWidth + 32)
    readonly property int dialogHeight: layout.implicitHeight + 16 + 12
    readonly property int textWidth: dialogWidth - 32 - 32

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
    }

    function choose(scope) {
        close()
        chosen(scope)
    }

    onVisibleChanged: {
        if (visible)
            laterButton.forceActiveFocus()
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

        Column {
            Layout.fillWidth: true
            Layout.rightMargin: 32
            spacing: 4

            FishUI.Label {
                width: control.textWidth
                text: control.title
                textFormat: Text.PlainText
                font.pixelSize: 14
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            FishUI.Label {
                width: control.textWidth
                text: control.text
                textFormat: Text.PlainText
                font.pixelSize: 14
                color: FishUI.Theme.disabledTextColor
                wrapMode: Text.Wrap
                lineHeight: 20
                lineHeightMode: Text.FixedHeight
                visible: text.length > 0
            }
        }

        RowLayout {
            id: actions
            Layout.alignment: Qt.AlignRight
            spacing: 8

            FishUI.ConfirmDialogButton {
                text: qsTr("Cancel")
                onClicked: control.close()
            }

            FishUI.ConfirmDialogButton {
                objectName: "thisEventButton"
                text: control.thisText
                visible: !control.seriesOnly
                onClicked: control.choose(0)
            }

            FishUI.ConfirmDialogButton {
                id: laterButton
                objectName: "laterEventsButton"
                text: control.laterText
                primary: true
                destructive: control.destructive
                onClicked: control.choose(1)
            }
        }
    }
}
