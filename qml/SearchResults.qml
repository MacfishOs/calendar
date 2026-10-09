// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FishUI as FishUI
import "Dates.js" as Dates

// Events whose title, location or notes match, coming ones first, under
// the day they fall on.
Item {
    id: panel
    objectName: "searchResults"

    required property var window
    property string text

    Rectangle {
        width: 1 / FishUI.Dpi.ratio
        height: parent.height
        color: Qt.alpha(FishUI.Theme.textColor, 0.07)
    }

    readonly property var results: {
        App.revision
        return App.search(text)
    }
    readonly property var rows: {
        const list = []
        let last = null
        for (const item of results) {
            const day = item.allDay ? item.firstDay : Dates.day(item.start)
            if (!last || !Dates.sameDay(last, day)) {
                list.push({ heading: true, date: day })
                last = day
            }
            list.push({ heading: false, event: item })
        }
        return list
    }

    function headingText(date) {
        if (Dates.sameDay(date, App.today))
            return qsTr("Today")
        if (Dates.sameDay(date, Dates.addDays(App.today, 1)))
            return qsTr("Tomorrow")
        return Dates.longDate(date)
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        clip: true
        model: panel.rows

        FishUI.WheelHandler {
            target: list
        }
        FishUI.ScrollBar.vertical: FishUI.ScrollBar {}

        delegate: Loader {
            required property var modelData
            width: list.width
            sourceComponent: modelData.heading ? heading : row

            Component {
                id: heading

                FishUI.Label {
                    width: list.width
                    height: 30
                    leftPadding: 16
                    rightPadding: 16
                    verticalAlignment: Text.AlignBottom
                    bottomPadding: 4
                    text: panel.headingText(modelData.date)
                    font.pointSize: 8
                    font.weight: Font.Medium
                    color: FishUI.Theme.disabledTextColor
                    elide: Text.ElideRight
                }
            }

            Component {
                id: row

                ItemDelegate {
                    id: delegate
                    readonly property var event: modelData.event
                    readonly property bool selected: panel.window.isSelected(event)

                    width: list.width
                    height: event.location.length > 0 ? 50 : 36
                    padding: 0
                    leftPadding: 16
                    rightPadding: 16
                    hoverEnabled: true
                    onClicked: panel.window.revealEvent(event)
                    onDoubleClicked: panel.window.editEvent(event)
                    Accessible.name: event.title

                    background: FishUI.RoundedRectangle {
                        x: 6
                        width: parent.width - 12
                        height: parent.height
                        continuousCorners: false
                        radius: FishUI.Theme.smallRadius
                        color: delegate.selected ? Qt.alpha(FishUI.Theme.highlightColor, FishUI.Theme.darkMode ? 0.25 : 0.12)
                             : delegate.hovered ? Qt.alpha(FishUI.Theme.textColor, 0.04) : "transparent"
                    }

                    contentItem: RowLayout {
                        spacing: 10

                        FishUI.RoundedRectangle {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 14
                            Layout.preferredWidth: 8
                            Layout.preferredHeight: 8
                            continuousCorners: false
                            radius: 4
                            color: delegate.event.color
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                FishUI.Label {
                                    Layout.fillWidth: true
                                    text: delegate.event.title.length > 0 ? delegate.event.title : qsTr("New Event")
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                }

                                FishUI.Label {
                                    text: delegate.event.allDay ? qsTr("All day") : delegate.event.startText
                                    font.pointSize: 8
                                    color: FishUI.Theme.disabledTextColor
                                }
                            }

                            FishUI.Label {
                                Layout.fillWidth: true
                                text: delegate.event.location
                                textFormat: Text.PlainText
                                font.pointSize: 8
                                color: FishUI.Theme.disabledTextColor
                                elide: Text.ElideRight
                                visible: text.length > 0
                            }
                        }
                    }
                }
            }
        }
    }

    FishUI.Label {
        anchors.centerIn: parent
        text: qsTr("No Events Found")
        color: FishUI.Theme.disabledTextColor
        visible: panel.results.length === 0
    }
}
