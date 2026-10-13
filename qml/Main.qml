// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import FishUI as FishUI
import "Dates.js" as Dates

FishUI.Window {
    id: rootWindow
    objectName: "calendarWindow"
    title: qsTr("Calendar")
    width: 1100
    height: 740
    minimumWidth: 640
    minimumHeight: 600
    visible: true
    contentTopMargin: 0
    header.height: 48
    windowButtonsTopMargin: applicationTitle.capCenter - header.buttonSize / 2
    blurEnabled: false
    background.color: FishUI.Theme.backgroundColor
    background.opacity: 1

    readonly property int sidebarWidth: 240
    // Controls on the calendar's surface get a track, as the segmented control has on cards.
    readonly property color controlTint: FishUI.Theme.darkMode ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.04)
    // Below this the sidebar leaves room for the calendar, and comes back above it.
    readonly property bool narrow: width < 900
    // Shown over the calendar while the window is narrow.
    property bool sidebarOverlay: false
    readonly property bool sidebarDocked: App.sidebarVisible && !narrow
    property real sidebarReveal: sidebarDocked || sidebarOverlay ? 1 : 0
    property real dockReveal: sidebarDocked ? 1 : 0

    Behavior on sidebarReveal {
        NumberAnimation {
            duration: 200
            easing.type: Easing.OutCubic
        }
    }

    Behavior on dockReveal {
        NumberAnimation {
            duration: 200
            easing.type: Easing.OutCubic
        }
    }

    onNarrowChanged: sidebarOverlay = false
    // The title and window buttons end on this line; content spacing is measured from it.
    readonly property int headerBaseline: 28
    readonly property var views: ["day", "week", "month", "year"]

    // The selected day; the views show the period around it.
    property date currentDate: Dates.day(new Date())
    readonly property string view: views.indexOf(App.view) >= 0 ? App.view : "month"
    readonly property date rangeStart: view === "week" ? Dates.weekStart(currentDate, App.firstDayOfWeek)
                                     : view === "day" ? currentDate
                                     : view === "year" ? new Date(currentDate.getFullYear(), 0, 1)
                                     : Dates.monthGridStart(currentDate, App.firstDayOfWeek)
    property bool preloadViews: false
    property string selectedKey: ""
    property string selectedRecurrenceKey: ""
    readonly property bool searching: search.text.trim().length > 0

    // The month or day in bold, the year in regular weight.
    readonly property string periodTitle: {
        switch (view) {
        case "day":
            return dayTitle(currentDate)
        case "year":
            return String(currentDate.getFullYear())
        case "week": {
            const last = Dates.addDays(rangeStart, 6)
            if (rangeStart.getMonth() === last.getMonth())
                return monthTitle(rangeStart)
            if (rangeStart.getFullYear() === last.getFullYear())
                return monthTitle(rangeStart, last)
            return monthTitle(rangeStart, null, true) + " – " + monthTitle(last, null, true)
        }
        default:
            return monthTitle(currentDate)
        }
    }

    function cjk() {
        const language = Qt.locale().name.split("_")[0]
        return language === "zh" || language === "ja" || language === "ko"
    }

    // Month and year in the order the language writes them; with `until`, a run of months.
    function monthTitle(date, until, abbreviated) {
        const brief = until || abbreviated
        const name = (month) => cjk() ? (month + 1) + (Qt.locale().name.startsWith("ko") ? "월" : "月")
                                      : Qt.locale().standaloneMonthName(month, brief ? Locale.ShortFormat
                                                                                     : Locale.LongFormat)
        const months = "<b>" + name(date.getMonth()) + (until ? " – " + name(until.getMonth()) : "") + "</b>"
        if (cjk())
            return date.getFullYear() + (Qt.locale().name.startsWith("ko") ? "년 " : "年") + months
        return months + " " + date.getFullYear()
    }

    // The language's long date, the weekday included and the year in regular weight.
    function dayTitle(date) {
        if (cjk()) {
            const day = date.getDate() + (Qt.locale().name.startsWith("ko") ? "일 " : "日 ")
                    + Qt.locale().standaloneDayName(date.getDay(), Locale.LongFormat)
            return monthTitle(date).replace("</b>", day + "</b>")
        }
        const format = Qt.locale().dateFormat(Locale.LongFormat).replace(/[,\s]*y+[.]?/, "").trim()
        return "<b>" + Qt.locale().toString(date, format) + "</b> " + date.getFullYear()
    }

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    function setView(name) {
        App.view = name
        focusView()
    }

    function step(direction) {
        switch (view) {
        case "day":
            currentDate = Dates.addDays(currentDate, direction)
            break
        case "week":
            currentDate = Dates.addDays(currentDate, 7 * direction)
            break
        case "year":
            currentDate = Dates.addMonths(currentDate, 12 * direction)
            break
        default:
            currentDate = Dates.addMonths(currentDate, direction)
        }
    }

    function goToday() {
        currentDate = Dates.day(App.today)
    }

    function showDay(date) {
        currentDate = Dates.day(date)
        setView("day")
    }

    function showMonth(date) {
        currentDate = Dates.day(date)
        setView("month")
    }

    function toggleSidebar() {
        if (narrow)
            sidebarOverlay = !sidebarOverlay
        else
            App.sidebarVisible = !App.sidebarVisible
    }

    function focusView() {
        keyTarget.forceActiveFocus()
    }

    function isSelected(item) {
        return !!item && item.key === selectedKey && item.recurrenceKey === selectedRecurrenceKey
    }

    function selectEvent(item) {
        selectedKey = item ? item.key : ""
        selectedRecurrenceKey = item ? item.recurrenceKey : ""
    }

    function revealEvent(item) {
        currentDate = Dates.day(item.allDay ? item.firstDay : item.start)
        selectEvent(item)
    }

    property QtObject editor: null

    function openEditor(data) {
        if (editor)
            editor.close()
        editor = editorComponent.createObject(rootWindow, { event: data })
        editor.show()
        editor.raise()
        editor.requestActivate()
    }

    function newEvent(start, end, allDay) {
        openEditor(App.newEventData(start, end, allDay))
    }

    // A timed hour on the day: the next whole hour today, nine o'clock otherwise.
    function newEventOn(date) {
        let hour = 9
        if (Dates.sameDay(date, App.today))
            hour = Math.min(23, new Date().getHours() + 1)
        const start = Dates.atMinute(date, hour * 60)
        newEvent(start, new Date(start.getTime() + 3600000), false)
    }

    function editEvent(item) {
        const data = App.eventData(item.key, item.recurrenceKey)
        if (data.key)
            openEditor(data)
    }

    function saveFromEditor(original, data) {
        if (!original.key || !original.recurring) {
            if (App.saveEvent(data, 2) && editor)
                editor.close()
            return
        }
        // A new rule or kind of event cannot apply to one occurrence alone.
        const seriesOnly = data.repeat !== original.repeat || data.allDay !== original.allDay
                || data.repeatEnd !== original.repeatEnd
                || (data.repeatEnd === 1 && !Dates.sameDay(data.repeatUntil, original.repeatUntil))
                || (data.repeatEnd === 2 && data.repeatCount !== original.repeatCount)
        askScope({
            title: qsTr("Save changes to this repeating event?"),
            text: seriesOnly ? qsTr("The new repeat applies to this and the following events.")
                             : qsTr("Change only this event, or this and the events after it?"),
            thisText: qsTr("Only This Event"),
            laterText: original.firstOccurrence ? qsTr("All Events") : qsTr("All Future Events"),
            seriesOnly: seriesOnly,
            destructive: false
        }, function (scope) {
            if (App.saveEvent(data, scope) && editor)
                editor.close()
        })
    }

    function deleteEvent(item) {
        if (!item || !item.key)
            return
        const title = item.title && item.title.length > 0 ? item.title : qsTr("New Event")
        const remove = function (scope) {
            if (!App.removeEvent(item.key, item.recurrenceKey, scope))
                return
            if (editor)
                editor.close()
            selectEvent(null)
            showPassiveNotification(qsTr("“%1” deleted").arg(title), 4000, qsTr("Undo"), function () {
                App.undo()
            })
        }
        if (!item.recurring) {
            remove(2)
            return
        }
        const first = App.isFirstOccurrence(item.key, item.recurrenceKey)
        askScope({
            title: qsTr("Delete “%1”?").arg(title),
            text: qsTr("This is a repeating event."),
            thisText: qsTr("Only This Event"),
            laterText: first ? qsTr("All Events") : qsTr("All Future Events"),
            seriesOnly: false,
            destructive: true
        }, remove)
    }

    // Dragging moves the start and the end by whole days and minutes.
    function moveEvent(item, startDays, startMinutes, endDays, endMinutes) {
        const move = function (scope) {
            App.moveOccurrence(item.key, item.recurrenceKey, startDays, startMinutes, endDays, endMinutes, scope)
        }
        if (!item.recurring) {
            // Later: the dragged item is rebuilt by the change.
            Qt.callLater(move, 2)
            return
        }
        const first = App.isFirstOccurrence(item.key, item.recurrenceKey)
        askScope({
            title: qsTr("Move this repeating event?"),
            text: qsTr("Move only this event, or this and the events after it?"),
            thisText: qsTr("Only This Event"),
            laterText: first ? qsTr("All Events") : qsTr("All Future Events"),
            seriesOnly: false,
            destructive: false
        }, move)
    }

    property var scopeCallback: null

    function askScope(options, callback) {
        scopeCallback = callback
        scopeDialog.title = options.title
        scopeDialog.text = options.text
        scopeDialog.thisText = options.thisText
        scopeDialog.laterText = options.laterText
        scopeDialog.seriesOnly = options.seriesOnly
        scopeDialog.destructive = options.destructive
        scopeDialog.transientParent = editor && editor.visible ? editor : rootWindow
        scopeDialog.open()
    }

    function newCalendar() {
        const dialog = calendarDialogComponent.createObject(rootWindow, {})
        dialog.open()
    }

    function editCalendar(calendar) {
        const dialog = calendarDialogComponent.createObject(rootWindow, {
            calendarId: calendar.id,
            name: calendar.name,
            calendarColor: calendar.color
        })
        dialog.open()
    }

    property var menuCalendar: null

    function showCalendarMenu(calendar) {
        menuCalendar = calendar
        calendarMenu.popup()
    }

    property var menuEvent: null

    function showEventMenu(item) {
        menuEvent = item
        eventMenu.popup()
    }

    property date menuDate

    function showDayMenu(date) {
        menuDate = date
        dayMenu.popup()
    }

    function importFiles(files) {
        importDialog.files = files
        importDialog.open()
    }

    function openDialog(name) {
        const dialog = (name === "settings" ? settingsComponent : aboutComponent).createObject(rootWindow)
        dialog.show()
        dialog.raise()
        dialog.requestActivate()
    }

    Connections {
        target: App
        function onDateRequested(date) {
            rootWindow.currentDate = Dates.day(date)
            if (rootWindow.view === "year")
                rootWindow.setView("day")
        }
        function onImportRequested(files) {
            rootWindow.importFiles(files.map(file => file.startsWith("/") ? "file://" + file : file))
        }
    }

    headerItem: Item {
        RowLayout {
            id: headerStart
            anchors.left: parent.left
            anchors.leftMargin: rootWindow.sidebarDocked ? 20 : 12
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            spacing: 12

            // The toggle ends at the sidebar's edge.
            FishUI.AlignedLabel {
                id: applicationTitle
                objectName: "applicationTitle"
                visible: rootWindow.sidebarDocked
                Layout.preferredWidth: rootWindow.sidebarWidth - 20 - 12 - rootWindow.header.buttonSize - 12
                textBaseline: rootWindow.headerBaseline
                text: rootWindow.title
                font.pointSize: 11
                color: rootWindow.active ? FishUI.Theme.textColor : FishUI.Theme.disabledTextColor
                elide: Text.ElideRight
            }

            FishUI.HeaderButton {
                id: sidebarButton
                objectName: "sidebarButton"
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: rootWindow.windowButtonsTopMargin
                size: rootWindow.header.buttonSize
                source: "qrc:/icons/sidebar.svg"
                image.color: tint
                checked: rootWindow.sidebarOverlay
                onClicked: rootWindow.toggleSidebar()
                Accessible.name: rootWindow.sidebarDocked || rootWindow.sidebarOverlay ? qsTr("Hide Sidebar")
                                                                                       : qsTr("Show Sidebar")
            }
        }

        FishUI.SegmentedControl {
            id: viewSwitch
            objectName: "viewSwitch"
            // Centred on the window, moved aside only as far as the buttons beside it need.
            readonly property real start: headerStart.x + headerStart.width + 12
            readonly property real end: headerEnd.x - 12
            width: Math.max(240, Math.min(300, end - start))
            height: 36
            // The sidebar shown over a narrow window has their room.
            visible: !rootWindow.sidebarOverlay
            x: Math.max(start, Math.min(end - width, (rootWindow.width - width) / 2))
            y: rootWindow.windowButtonsTopMargin + (rootWindow.header.buttonSize - height) / 2
            currentIndex: rootWindow.views.indexOf(rootWindow.view)
            onCurrentIndexChanged: {
                if (currentIndex >= 0 && rootWindow.views[currentIndex] !== rootWindow.view)
                    rootWindow.setView(rootWindow.views[currentIndex])
            }

            background: FishUI.RoundedRectangle {
                continuousCorners: false
                color: rootWindow.controlTint
                radius: height / 2
            }

            FishUI.TabButton {
                text: qsTr("Day")
                font.pointSize: 9
            }
            FishUI.TabButton {
                text: qsTr("Week")
                font.pointSize: 9
            }
            FishUI.TabButton {
                text: qsTr("Month")
                font.pointSize: 9
            }
            FishUI.TabButton {
                text: qsTr("Year")
                font.pointSize: 9
            }
        }

        RowLayout {
            id: headerEnd
            anchors.right: parent.right
            anchors.rightMargin: rootWindow.windowButtonsSpacing
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            spacing: 12

            FishUI.TextField {
                id: search
                objectName: "searchField"
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: rootWindow.windowButtonsTopMargin + (rootWindow.header.buttonSize - height) / 2
                Layout.preferredWidth: rootWindow.width >= 1000 ? 220 : 160
                Layout.preferredHeight: 30
                opacity: rootWindow.sidebarOverlay ? 0 : 1
                enabled: !rootWindow.sidebarOverlay
                leftPadding: 34
                rightPadding: text.length > 0 ? 34 : 12
                topPadding: 0
                bottomPadding: 0
                placeholderText: qsTr("Search events")
                font.pointSize: 9
                Accessible.name: qsTr("Search events")
                Keys.onEscapePressed: {
                    search.clear()
                    rootWindow.focusView()
                }

                background: FishUI.RoundedRectangle {
                    continuousCorners: false
                    radius: height / 2
                    color: rootWindow.controlTint
                    border.width: search.activeFocus ? 1 : 0
                    border.color: FishUI.Theme.highlightColor

                    FishUI.RoundedRectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        continuousCorners: false
                        radius: parent.height / 2
                        inset: -2
                        color: "transparent"
                        border.width: 2
                        border.color: FishUI.Theme.highlightColor
                        opacity: 0.25
                        visible: search.activeFocus
                    }
                }

                FishUI.Icon {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    name: "search"
                    color: FishUI.Theme.disabledTextColor
                }

                FishUI.Icon {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    visible: search.text.length > 0
                    name: "clear"
                    color: FishUI.Theme.disabledTextColor
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Clear search")
                    Accessible.onPressAction: search.clear()

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -7
                        onClicked: search.clear()
                    }
                }
            }

            FishUI.HeaderButton {
                id: menuButton
                objectName: "menuButton"
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: rootWindow.windowButtonsTopMargin
                size: rootWindow.header.buttonSize
                artwork: "menu"
                checked: mainMenu.visible
                onClicked: mainMenu.popupFor(menuButton.background, FishUI.Units.smallSpacing / 2)
                Accessible.name: qsTr("Menu")
            }
        }
    }

    // The calendar's side of the window, title bar included; the sidebar keeps the window colour.
    Rectangle {
        x: rootWindow.sidebarWidth * rootWindow.dockReveal
        width: rootWindow.width - x
        height: rootWindow.height
        color: FishUI.Theme.secondBackgroundColor
    }

    // Over the calendar the sidebar needs its own background and edge, title bar included.
    Rectangle {
        x: sidebar.x
        z: 2
        width: rootWindow.sidebarWidth
        height: rootWindow.height
        color: FishUI.Theme.backgroundColor
        visible: sidebar.visible && !rootWindow.sidebarDocked

        Rectangle {
            anchors.right: parent.right
            width: 1 / FishUI.Dpi.ratio
            height: parent.height
            color: Qt.alpha(FishUI.Theme.textColor, 0.1)
        }
    }

    Sidebar {
        id: sidebar
        window: rootWindow
        x: -rootWindow.sidebarWidth * (1 - rootWindow.sidebarReveal)
        z: 2
        anchors.top: parent.top
        anchors.topMargin: rootWindow.header.height
        anchors.bottom: parent.bottom
        width: rootWindow.sidebarWidth
        visible: rootWindow.sidebarReveal > 0
    }

    FocusScope {
        id: keyTarget
        objectName: "keyTarget"
        anchors.left: parent.left
        anchors.leftMargin: rootWindow.sidebarWidth * rootWindow.dockReveal
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: rootWindow.header.height
        anchors.bottom: parent.bottom
        focus: true

        // A press on the calendar puts away the sidebar shown over it.
        PointHandler {
            enabled: rootWindow.sidebarOverlay
            onActiveChanged: {
                if (active)
                    rootWindow.sidebarOverlay = false
            }
        }

        Keys.onPressed: (event) => {
            const days = { "day": [1, 1], "week": [1, 7], "month": [1, 7], "year": [1, 7] }[rootWindow.view]
            let move = 0
            if (event.key === Qt.Key_Left)
                move = -days[0]
            else if (event.key === Qt.Key_Right)
                move = days[0]
            else if (event.key === Qt.Key_Up)
                move = -days[1]
            else if (event.key === Qt.Key_Down)
                move = days[1]
            if (move !== 0 && event.modifiers === Qt.NoModifier) {
                rootWindow.currentDate = Dates.addDays(rootWindow.currentDate, move)
                rootWindow.selectEvent(null)
                event.accepted = true
            } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && rootWindow.selectedKey) {
                rootWindow.editEvent({ key: rootWindow.selectedKey, recurrenceKey: rootWindow.selectedRecurrenceKey })
                event.accepted = true
            } else if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) && rootWindow.selectedKey) {
                const data = App.eventData(rootWindow.selectedKey, rootWindow.selectedRecurrenceKey)
                data.key = rootWindow.selectedKey
                rootWindow.deleteEvent(data)
                event.accepted = true
            } else if (event.key === Qt.Key_Escape && rootWindow.selectedKey) {
                rootWindow.selectEvent(null)
                event.accepted = true
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 4
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 12
                spacing: 8

                // Capitals centred on the buttons beside it.
                FishUI.AlignedLabel {
                    objectName: "periodTitle"
                    Layout.fillWidth: true
                    textBaseline: previousButton.height / 2 + capHeight / 2
                    text: rootWindow.periodTitle
                    textFormat: Text.StyledText
                    font.pointSize: 18
                    elide: Text.ElideRight
                }

                NavigationButton {
                    id: previousButton
                    objectName: "previousButton"
                    Layout.alignment: Qt.AlignTop
                    iconName: "chevron-left"
                    onClicked: rootWindow.step(-1)
                    Accessible.name: qsTr("Previous")
                }

                NavigationButton {
                    objectName: "todayButton"
                    Layout.alignment: Qt.AlignTop
                    text: qsTr("Today")
                    onClicked: rootWindow.goToday()
                }

                NavigationButton {
                    objectName: "nextButton"
                    Layout.alignment: Qt.AlignTop
                    iconName: "chevron-right"
                    onClicked: rootWindow.step(1)
                    Accessible.name: qsTr("Next")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Item {
                    objectName: "viewArea"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    Item {
                        anchors.fill: parent

                        // Views stay built once shown, and the others are built while idle,
                        // so switching never waits for a view to be created.
                        Repeater {
                            model: [
                                { name: "day", component: dayComponent },
                                { name: "week", component: weekComponent },
                                { name: "month", component: monthComponent },
                                { name: "year", component: yearComponent }
                            ]

                            Loader {
                                required property var modelData
                                readonly property bool current: rootWindow.view === modelData.name
                                property bool shown: false

                                anchors.fill: parent
                                opacity: current ? 1 : 0
                                enabled: current
                                asynchronous: !current
                                active: current || shown || rootWindow.preloadViews
                                onCurrentChanged: shown = true
                                sourceComponent: modelData.component
                                onLoaded: item.live = Qt.binding(() => current)
                            }
                        }
                    }
                }

                SearchResults {
                    Layout.preferredWidth: 300
                    Layout.fillHeight: true
                    visible: rootWindow.searching
                    window: rootWindow
                    text: search.text
                }
            }
        }
    }

    Component {
        id: monthComponent
        MonthView {
            window: rootWindow
        }
    }

    Component {
        id: weekComponent
        TimeGridView {
            window: rootWindow
            days: 7
        }
    }

    Component {
        id: dayComponent
        TimeGridView {
            window: rootWindow
            days: 1
        }
    }

    Component {
        id: yearComponent
        YearView {
            window: rootWindow
        }
    }

    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (drop) => {
            const files = drop.urls.map(url => String(url)).filter(url => url.toLowerCase().endsWith(".ics"))
            if (files.length > 0) {
                drop.accept(Qt.CopyAction)
                rootWindow.importFiles(files)
            }
        }
    }

    FishUI.DesktopMenu {
        id: mainMenu
        objectName: "mainMenu"
        pointerGrabEnabled: true

        FishUI.MenuItem {
            text: qsTr("New Event")
            shortcut: StandardKey.New
            onTriggered: rootWindow.newEventOn(rootWindow.currentDate)
        }

        FishUI.MenuItem {
            text: qsTr("New Calendar…")
            onTriggered: rootWindow.newCalendar()
        }

        FishUI.MenuItem {
            text: qsTr("Import…")
            onTriggered: importFileDialog.open()
        }

        FishUI.MenuSeparator {}

        FishUI.MenuItem {
            text: App.canUndo ? App.undoText : qsTr("Undo")
            shortcut: StandardKey.Undo
            enabled: App.canUndo
            onTriggered: App.undo()
        }

        FishUI.MenuSeparator {}

        FishUI.MenuItem {
            text: qsTr("Settings")
            shortcut: "Ctrl+,"
            onTriggered: rootWindow.openDialog("settings")
        }

        FishUI.MenuItem {
            text: qsTr("About Calendar")
            onTriggered: rootWindow.openDialog("about")
        }
    }

    FishUI.DesktopMenu {
        id: eventMenu
        objectName: "eventMenu"
        pointerGrabEnabled: true

        FishUI.MenuItem {
            text: qsTr("Edit…")
            onTriggered: rootWindow.editEvent(rootWindow.menuEvent)
        }

        FishUI.MenuSeparator {}

        FishUI.MenuItem {
            text: qsTr("Delete")
            onTriggered: rootWindow.deleteEvent(rootWindow.menuEvent)
        }
    }

    FishUI.DesktopMenu {
        id: dayMenu
        objectName: "dayMenu"
        pointerGrabEnabled: true

        FishUI.MenuItem {
            text: qsTr("New Event")
            onTriggered: rootWindow.newEventOn(rootWindow.menuDate)
        }

        FishUI.MenuItem {
            text: qsTr("New All-Day Event")
            onTriggered: rootWindow.newEvent(rootWindow.menuDate, rootWindow.menuDate, true)
        }
    }

    FishUI.DesktopMenu {
        id: calendarMenu
        objectName: "calendarMenu"
        pointerGrabEnabled: true

        FishUI.MenuItem {
            text: qsTr("Edit…")
            onTriggered: rootWindow.editCalendar(rootWindow.menuCalendar)
        }

        FishUI.MenuItem {
            text: qsTr("Export…")
            onTriggered: {
                exportFileDialog.calendarId = rootWindow.menuCalendar.id
                exportFileDialog.selectedFile = "file://" + App.exportFileName(rootWindow.menuCalendar.id)
                exportFileDialog.open()
            }
        }

        FishUI.MenuSeparator {}

        FishUI.MenuItem {
            text: qsTr("Delete…")
            enabled: App.calendars.length > 1
            onTriggered: {
                deleteCalendarDialog.calendar = rootWindow.menuCalendar
                deleteCalendarDialog.open()
            }
        }
    }

    Component {
        id: editorComponent

        EventDialog {
            id: dialog
            parentWindow: rootWindow
            transientParent: rootWindow
            onSaveRequested: (data) => rootWindow.saveFromEditor(dialog.event, data)
            onDeleteRequested: rootWindow.deleteEvent({
                key: dialog.event.key,
                recurrenceKey: dialog.event.recurrenceKey,
                recurring: dialog.event.recurring,
                title: dialog.event.title
            })
            onClosing: {
                if (rootWindow.editor === dialog)
                    rootWindow.editor = null
                dialog.destroy()
            }
        }
    }

    ScopeDialog {
        id: scopeDialog
        parentWindow: rootWindow
        onChosen: (scope) => {
            const callback = rootWindow.scopeCallback
            rootWindow.scopeCallback = null
            if (callback)
                callback(scope)
        }
    }

    Component {
        id: calendarDialogComponent

        CalendarDialog {
            id: calendarDialog
            parentWindow: rootWindow
            transientParent: rootWindow
            onClosing: calendarDialog.destroy()
        }
    }

    FishUI.ConfirmDialog {
        id: deleteCalendarDialog
        objectName: "deleteCalendarDialog"
        parentWindow: rootWindow
        showOnCompleted: false
        property var calendar: null
        title: calendar ? qsTr("Delete “%1”?").arg(calendar.name) : ""
        text: !calendar ? ""
              : App.eventCount(calendar.id) === 1 ? qsTr("Its event is moved to the trash.")
              : qsTr("Its %1 events are moved to the trash.").arg(App.eventCount(calendar.id))
        confirmText: qsTr("Delete")
        destructive: true
        onAccepted: App.removeCalendar(calendar.id)
    }

    ImportDialog {
        id: importDialog
        parentWindow: rootWindow
        transientParent: rootWindow
        onImportRequested: (files, calendarId) => {
            let total = 0
            let failed = 0
            for (const file of files) {
                const count = App.importFile(file, calendarId)
                if (count < 0)
                    ++failed
                else
                    total += count
            }
            if (failed > 0 && total === 0)
                rootWindow.showPassiveNotification(qsTr("No events could be read from the file."))
            else
                rootWindow.showPassiveNotification(total === 1 ? qsTr("1 event imported")
                                                               : qsTr("%1 events imported").arg(total))
        }
    }

    FileDialog {
        id: importFileDialog
        title: qsTr("Import")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Calendar files (*.ics)"), qsTr("All files (*)")]
        onAccepted: rootWindow.importFiles(selectedFiles.map(url => String(url)))
    }

    FileDialog {
        id: exportFileDialog
        property string calendarId
        title: qsTr("Export")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "ics"
        nameFilters: [qsTr("Calendar files (*.ics)")]
        onAccepted: {
            if (!App.exportCalendar(calendarId, selectedFile))
                rootWindow.showPassiveNotification(qsTr("The calendar could not be exported."))
        }
    }

    Component {
        id: settingsComponent
        SettingsDialog {
            id: settings
            parentWindow: rootWindow
            transientParent: rootWindow
            onClosing: settings.destroy()
        }
    }

    Component {
        id: aboutComponent
        FishUI.AboutDialog {
            id: about
            parentWindow: rootWindow
            name: qsTr("Calendar")
            description: qsTr("Keeps track of your events.")
            iconSource: "image://icontheme/office-calendar"
            link: "https://github.com/cutefishos/calendar"

            Shortcut {
                sequence: "Escape"
                onActivated: about.close()
            }
        }
    }

    Timer {
        interval: 1000
        running: true
        onTriggered: rootWindow.preloadViews = true
    }

    Shortcut {
        sequence: "Escape"
        enabled: rootWindow.sidebarOverlay
        onActivated: rootWindow.sidebarOverlay = false
    }

    Shortcut { sequences: [StandardKey.New]; onActivated: rootWindow.newEventOn(rootWindow.currentDate) }
    Shortcut { sequences: [StandardKey.Find]; onActivated: search.forceActiveFocus() }
    Shortcut { sequences: ["Ctrl+T", "Home"]; onActivated: rootWindow.goToday() }
    Shortcut { sequences: ["PgUp", "Alt+Left"]; onActivated: rootWindow.step(-1) }
    Shortcut { sequences: ["PgDown", "Alt+Right"]; onActivated: rootWindow.step(1) }
    Shortcut { sequence: "Ctrl+1"; onActivated: rootWindow.setView("day") }
    Shortcut { sequence: "Ctrl+2"; onActivated: rootWindow.setView("week") }
    Shortcut { sequence: "Ctrl+3"; onActivated: rootWindow.setView("month") }
    Shortcut { sequence: "Ctrl+4"; onActivated: rootWindow.setView("year") }
    Shortcut { sequences: [StandardKey.Undo]; enabled: App.canUndo; onActivated: App.undo() }
    Shortcut { sequence: "Ctrl+,"; onActivated: rootWindow.openDialog("settings") }
}
