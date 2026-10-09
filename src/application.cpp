// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.h"
#include "eventlayout.h"
#include "preferences.h"

#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>

static const int UndoDepth = 20;
// How far ahead search looks for a repeating event's next occurrence, in days.
static const int SearchHorizon = 3 * 366;

static QDate dateOf(const QDateTime &dateTime)
{
    return dateTime.toLocalTime().date();
}

static QDateTime startOfDay(QDate date)
{
    return QDateTime(date, QTime(0, 0));
}

static EventData eventDataFrom(const QVariantMap &map)
{
    EventData data;
    data.summary = map.value(QStringLiteral("title")).toString();
    data.location = map.value(QStringLiteral("location")).toString();
    data.description = map.value(QStringLiteral("notes")).toString();
    data.allDay = map.value(QStringLiteral("allDay")).toBool();
    const QDateTime start = map.value(QStringLiteral("start")).toDateTime().toLocalTime();
    const QDateTime end = map.value(QStringLiteral("end")).toDateTime().toLocalTime();
    if (data.allDay) {
        data.startDate = start.date();
        data.endDate = qMax(end.date(), start.date());
    } else {
        data.start = start;
        data.end = qMax(end, start);
    }

    data.repeat.frequency = Repeat::Frequency(map.value(QStringLiteral("repeat")).toInt());
    switch (map.value(QStringLiteral("repeatEnd")).toInt()) {
    case 1:
        data.repeat.until = dateOf(map.value(QStringLiteral("repeatUntil")).toDateTime());
        break;
    case 2:
        data.repeat.count = qMax(1, map.value(QStringLiteral("repeatCount")).toInt());
        break;
    default:
        break;
    }
    data.alarm = EventData::Alarm(map.value(QStringLiteral("alarm")).toInt());
    data.alarmOffset = map.value(QStringLiteral("alarmOffset")).toInt();
    return data;
}

static QVariantMap mapFrom(const EventData &data)
{
    QVariantMap map;
    map.insert(QStringLiteral("title"), data.summary);
    map.insert(QStringLiteral("location"), data.location);
    map.insert(QStringLiteral("notes"), data.description);
    map.insert(QStringLiteral("allDay"), data.allDay);
    map.insert(QStringLiteral("start"), data.allDay ? startOfDay(data.startDate) : data.start);
    map.insert(QStringLiteral("end"), data.allDay ? startOfDay(data.endDate) : data.end);
    map.insert(QStringLiteral("repeat"), int(data.repeat.frequency));
    map.insert(QStringLiteral("repeatEnd"), data.repeat.count > 0 ? 2 : data.repeat.until.isValid() ? 1 : 0);
    map.insert(QStringLiteral("repeatUntil"), startOfDay(data.repeat.until.isValid() ? data.repeat.until
                                                         : (data.allDay ? data.startDate : data.start.date()).addMonths(1)));
    map.insert(QStringLiteral("repeatCount"), data.repeat.count > 0 ? data.repeat.count : 10);
    map.insert(QStringLiteral("alarm"), int(data.alarm));
    map.insert(QStringLiteral("alarmOffset"), data.alarmOffset);
    return map;
}

// The fields of the whole series when `data` is what one occurrence should become.
static EventData seriesData(const EventFile &file, const QString &recurrenceKey, const EventData &data)
{
    EventData series = file.data();
    if (recurrenceKey.isEmpty()) {
        series = data;
        return series;
    }

    std::unique_ptr<EventFile> original = file.clone();
    original->resetOccurrence(recurrenceKey);
    const EventData instance = original->data(recurrenceKey);

    EventData result = data;
    const QDate instanceDay = instance.allDay ? instance.startDate : instance.start.date();
    const QDate seriesDay = series.allDay ? series.startDate : series.start.date();
    if (data.allDay) {
        result.startDate = seriesDay.addDays(instanceDay.daysTo(data.startDate));
        result.endDate = result.startDate.addDays(data.startDate.daysTo(data.endDate));
    } else if (instance.allDay) {
        result.start = QDateTime(seriesDay.addDays(instanceDay.daysTo(data.start.date())), data.start.time());
        result.end = result.start.addSecs(data.start.secsTo(data.end));
    } else {
        result.start = series.start.addSecs(instance.start.secsTo(data.start));
        result.end = result.start.addSecs(data.start.secsTo(data.end));
    }
    return result;
}

Application::Application(CalendarStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_settings(Preferences::ownFile(), QSettings::IniFormat)
    , m_today(QDate::currentDate())
{
    m_weekStart = qBound(0, m_settings.value(QStringLiteral("weekStart"), 0).toInt(), 7);
    m_showWeekNumbers = m_settings.value(QStringLiteral("showWeekNumbers"), false).toBool();
    m_view = m_settings.value(QStringLiteral("view"), QStringLiteral("month")).toString();
    m_sidebarVisible = m_settings.value(QStringLiteral("sidebarVisible"), true).toBool();
    const QStringList hidden = m_settings.value(QStringLiteral("Calendars/hidden")).toStringList();
    m_hidden = QSet<QString>(hidden.begin(), hidden.end());

    connect(m_store, &CalendarStore::readyChanged, this, [this] {
        ensureCalendar();
        Q_EMIT readyChanged();
    });
    connect(m_store, &CalendarStore::changed, this, [this] {
        ++m_revision;
        Q_EMIT revisionChanged();
    });

    // A day changes at midnight, after a suspend or when the clock is set.
    m_dayTimer.setInterval(30 * 1000);
    m_dayTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_dayTimer, &QTimer::timeout, this, [this] {
        if (QDate::currentDate() != m_today) {
            m_today = QDate::currentDate();
            Q_EMIT todayChanged();
        }
    });
    m_dayTimer.start();
}

void Application::ensureCalendar()
{
    if (m_store->calendars().isEmpty())
        m_store->createCalendar(tr("Personal"), QColor(0x33, 0x85, 0xff));
}

QVariantList Application::calendars() const
{
    QVariantList list;
    const QList<CalendarInfo> calendars = m_store->calendars();
    for (const CalendarInfo &calendar : calendars) {
        list.append(QVariantMap {
            { QStringLiteral("id"), calendar.id },
            { QStringLiteral("name"), calendar.name },
            { QStringLiteral("color"), calendar.color },
            { QStringLiteral("visible"), !m_hidden.contains(calendar.id) },
        });
    }
    return list;
}

QString Application::defaultCalendar() const
{
    const QString id = m_settings.value(QStringLiteral("defaultCalendar")).toString();
    if (m_store->hasCalendar(id))
        return id;
    const QList<CalendarInfo> calendars = m_store->calendars();
    return calendars.isEmpty() ? QString() : calendars.first().id;
}

void Application::setDefaultCalendar(const QString &id)
{
    if (id == m_settings.value(QStringLiteral("defaultCalendar")).toString())
        return;
    m_settings.setValue(QStringLiteral("defaultCalendar"), id);
    Q_EMIT defaultCalendarChanged();
}

void Application::setWeekStart(int day)
{
    day = qBound(0, day, 7);
    if (day == m_weekStart)
        return;
    m_weekStart = day;
    m_settings.setValue(QStringLiteral("weekStart"), day);
    Q_EMIT weekStartChanged();
}

int Application::firstDayOfWeek() const
{
    return m_weekStart > 0 ? m_weekStart : int(QLocale().firstDayOfWeek());
}

void Application::setShowWeekNumbers(bool show)
{
    if (show == m_showWeekNumbers)
        return;
    m_showWeekNumbers = show;
    m_settings.setValue(QStringLiteral("showWeekNumbers"), show);
    Q_EMIT showWeekNumbersChanged();
}

bool Application::use24Hour() const
{
    const QString format = QLocale().timeFormat(QLocale::ShortFormat);
    return !format.contains(QLatin1Char('a'), Qt::CaseInsensitive);
}

void Application::setSidebarVisible(bool visible)
{
    if (visible == m_sidebarVisible)
        return;
    m_sidebarVisible = visible;
    m_settings.setValue(QStringLiteral("sidebarVisible"), visible);
    Q_EMIT sidebarVisibleChanged();
}

QVariantList Application::weekendDays() const
{
    const QList<Qt::DayOfWeek> weekdays = QLocale().weekdays();
    QVariantList days;
    for (int day = Qt::Monday; day <= Qt::Sunday; ++day) {
        if (!weekdays.contains(Qt::DayOfWeek(day)))
            days.append(day % 7);
    }
    return days;
}

void Application::setView(const QString &view)
{
    if (view == m_view)
        return;
    m_view = view;
    m_settings.setValue(QStringLiteral("view"), view);
    Q_EMIT viewChanged();
}

QSet<QString> Application::hiddenCalendars() const
{
    return m_hidden;
}

QVariantMap Application::occurrenceMap(const Occurrence &occurrence) const
{
    const QLocale locale;
    QVariantMap map;
    map.insert(QStringLiteral("key"), occurrence.key);
    map.insert(QStringLiteral("recurrenceKey"), occurrence.recurrenceKey);
    map.insert(QStringLiteral("title"), occurrence.summary);
    map.insert(QStringLiteral("location"), occurrence.location);
    map.insert(QStringLiteral("allDay"), occurrence.allDay);
    map.insert(QStringLiteral("recurring"), occurrence.recurring);
    map.insert(QStringLiteral("color"), m_store->calendar(occurrence.calendarId).color);
    map.insert(QStringLiteral("calendarId"), occurrence.calendarId);
    map.insert(QStringLiteral("firstDay"), startOfDay(occurrence.firstDay));
    map.insert(QStringLiteral("lastDay"), startOfDay(occurrence.lastDay));
    if (!occurrence.allDay) {
        map.insert(QStringLiteral("start"), occurrence.start);
        map.insert(QStringLiteral("end"), occurrence.end);
        map.insert(QStringLiteral("startText"), locale.toString(occurrence.start.time(), QLocale::ShortFormat));
        map.insert(QStringLiteral("endText"), locale.toString(occurrence.end.time(), QLocale::ShortFormat));
    }
    return map;
}

QVariantMap Application::monthLayout(const QDateTime &first, int rows, int lanes) const
{
    const QDate start = dateOf(first);
    const QList<Occurrence> occurrences = m_store->occurrences(start, start.addDays(qint64(rows) * 7), m_hidden);
    const EventLayout::Rows layout = EventLayout::layoutRows(occurrences, start, rows, lanes);

    QVariantList bars;
    for (const EventLayout::Bar &bar : layout.bars) {
        QVariantMap map = occurrenceMap(occurrences.at(bar.index));
        map.insert(QStringLiteral("row"), bar.row);
        map.insert(QStringLiteral("column"), bar.column);
        map.insert(QStringLiteral("span"), bar.span);
        map.insert(QStringLiteral("lane"), bar.lane);
        map.insert(QStringLiteral("continuesBefore"), bar.continuesBefore);
        map.insert(QStringLiteral("continuesAfter"), bar.continuesAfter);
        map.insert(QStringLiteral("bar"), occurrences.at(bar.index).allDay
                   || occurrences.at(bar.index).firstDay != occurrences.at(bar.index).lastDay);
        bars.append(map);
    }
    QVariantList more;
    for (const EventLayout::More &item : layout.more) {
        more.append(QVariantMap {
            { QStringLiteral("row"), item.row },
            { QStringLiteral("column"), item.column },
            { QStringLiteral("count"), item.count },
            { QStringLiteral("date"), startOfDay(start.addDays(qint64(item.row) * 7 + item.column)) },
        });
    }
    return { { QStringLiteral("bars"), bars }, { QStringLiteral("more"), more } };
}

QVariantMap Application::dayLayout(const QDateTime &first, int days, int lanes) const
{
    const QDate start = dateOf(first);
    const QList<Occurrence> occurrences = m_store->occurrences(start, start.addDays(days), m_hidden);
    const EventLayout::Rows strip = EventLayout::layoutRows(occurrences, start, 1, lanes, true, days);

    QVariantList bars;
    for (const EventLayout::Bar &bar : strip.bars) {
        QVariantMap map = occurrenceMap(occurrences.at(bar.index));
        map.insert(QStringLiteral("column"), bar.column);
        map.insert(QStringLiteral("span"), bar.span);
        map.insert(QStringLiteral("lane"), bar.lane);
        map.insert(QStringLiteral("continuesBefore"), bar.continuesBefore);
        map.insert(QStringLiteral("continuesAfter"), bar.continuesAfter);
        bars.append(map);
    }
    QVariantList more;
    for (const EventLayout::More &item : strip.more) {
        more.append(QVariantMap {
            { QStringLiteral("column"), item.column },
            { QStringLiteral("count"), item.count },
            { QStringLiteral("date"), startOfDay(start.addDays(item.column)) },
        });
    }

    QVariantList blocks;
    const QList<EventLayout::Block> layout = EventLayout::layoutDays(occurrences, start, days, 30);
    for (const EventLayout::Block &block : layout) {
        QVariantMap map = occurrenceMap(occurrences.at(block.index));
        map.insert(QStringLiteral("column"), block.column);
        map.insert(QStringLiteral("startMinute"), block.startMinute);
        map.insert(QStringLiteral("endMinute"), block.endMinute);
        map.insert(QStringLiteral("slot"), block.slot);
        map.insert(QStringLiteral("slots"), block.slotCount);
        map.insert(QStringLiteral("span"), block.span);
        map.insert(QStringLiteral("continuesBefore"), block.continuesBefore);
        map.insert(QStringLiteral("continuesAfter"), block.continuesAfter);
        blocks.append(map);
    }

    return {
        { QStringLiteral("bars"), bars },
        { QStringLiteral("more"), more },
        { QStringLiteral("lanes"), strip.lanes },
        { QStringLiteral("blocks"), blocks },
    };
}

QVariantList Application::busyDays(const QDateTime &first, int days) const
{
    const QDate start = dateOf(first);
    QVector<bool> busy(days, false);
    const QList<Occurrence> occurrences = m_store->occurrences(start, start.addDays(days), m_hidden);
    for (const Occurrence &occurrence : occurrences) {
        const qint64 from = qMax<qint64>(0, start.daysTo(occurrence.firstDay));
        const qint64 to = qMin<qint64>(days - 1, start.daysTo(occurrence.lastDay));
        for (qint64 day = from; day <= to; ++day)
            busy[day] = true;
    }
    QVariantList list;
    list.reserve(days);
    for (bool day : std::as_const(busy))
        list.append(day);
    return list;
}

QVariantList Application::search(const QString &text) const
{
    const QString needle = text.trimmed();
    if (needle.isEmpty())
        return {};

    struct Match
    {
        Occurrence occurrence;
        QDateTime start;
        bool upcoming;
    };
    QList<Match> matches;
    const QDateTime now = QDateTime::currentDateTime();
    const QStringList keys = m_store->keys();
    for (const QString &key : keys) {
        const QString calendarId = CalendarStore::calendarOf(key);
        if (m_hidden.contains(calendarId))
            continue;
        const EventFile *file = m_store->event(key);
        if (!file->summary().contains(needle, Qt::CaseInsensitive)
                && !file->location().contains(needle, Qt::CaseInsensitive)
                && !file->description().contains(needle, Qt::CaseInsensitive))
            continue;

        Match match;
        match.upcoming = file->nextOccurrence(QDateTime(now.date(), QTime(0, 0)), SearchHorizon, &match.occurrence);
        if (!match.upcoming) {
            // Over by now: shown at its first occurrence.
            const EventData data = file->data();
            QList<Occurrence> first;
            const QDate day = data.allDay ? data.startDate : data.start.date();
            file->expand(day, day.addDays(1), &first);
            if (first.isEmpty())
                continue;
            match.occurrence = first.first();
        }
        match.occurrence.key = key;
        match.occurrence.calendarId = calendarId;
        match.start = match.occurrence.allDay ? startOfDay(match.occurrence.firstDay) : match.occurrence.start;
        matches.append(match);
    }

    // What is coming first, soonest first, then what is over, latest first.
    std::sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
        if (a.upcoming != b.upcoming)
            return a.upcoming;
        return a.upcoming ? a.start < b.start : a.start > b.start;
    });
    QVariantList list;
    for (const Match &match : std::as_const(matches)) {
        QVariantMap map = occurrenceMap(match.occurrence);
        map.insert(QStringLiteral("upcoming"), match.upcoming);
        list.append(map);
    }
    return list;
}

int Application::weekNumber(const QDateTime &rowStart) const
{
    // The ISO week of the row's Thursday, whichever day the row starts on.
    const QDate start = dateOf(rowStart);
    for (int i = 0; i < 7; ++i) {
        if (start.addDays(i).dayOfWeek() == Qt::Thursday)
            return start.addDays(i).weekNumber();
    }
    return start.weekNumber();
}

QVariantMap Application::eventData(const QString &key, const QString &recurrenceKey) const
{
    const EventFile *file = m_store->event(key);
    if (!file)
        return {};
    QVariantMap map = mapFrom(file->data(recurrenceKey));
    map.insert(QStringLiteral("key"), key);
    map.insert(QStringLiteral("recurrenceKey"), recurrenceKey);
    map.insert(QStringLiteral("calendarId"), CalendarStore::calendarOf(key));
    map.insert(QStringLiteral("recurring"), file->isRecurring());
    map.insert(QStringLiteral("firstOccurrence"), recurrenceKey.isEmpty() || file->isFirstOccurrence(recurrenceKey));
    return map;
}

QVariantMap Application::newEventData(const QDateTime &start, const QDateTime &end, bool allDay) const
{
    EventData data;
    data.allDay = allDay;
    if (allDay) {
        data.startDate = dateOf(start);
        data.endDate = qMax(dateOf(end), data.startDate);
    } else {
        data.start = start.toLocalTime();
        data.end = qMax(end.toLocalTime(), data.start);
    }
    QVariantMap map = mapFrom(data);
    map.insert(QStringLiteral("key"), QString());
    map.insert(QStringLiteral("recurrenceKey"), QString());
    map.insert(QStringLiteral("calendarId"), defaultCalendar());
    map.insert(QStringLiteral("recurring"), false);
    map.insert(QStringLiteral("firstOccurrence"), true);
    return map;
}

bool Application::isFirstOccurrence(const QString &key, const QString &recurrenceKey) const
{
    const EventFile *file = m_store->event(key);
    return !file || recurrenceKey.isEmpty() || file->isFirstOccurrence(recurrenceKey);
}

bool Application::saveEvent(const QVariantMap &map, int scope)
{
    const QString key = map.value(QStringLiteral("key")).toString();
    const QString recurrenceKey = map.value(QStringLiteral("recurrenceKey")).toString();
    QString calendarId = map.value(QStringLiteral("calendarId")).toString();
    if (!m_store->hasCalendar(calendarId))
        calendarId = defaultCalendar();
    const EventData data = eventDataFrom(map);

    if (key.isEmpty()) {
        const QString added = m_store->addEvent(calendarId, EventFile::create(data));
        if (added.isEmpty())
            return false;
        setDefaultCalendar(calendarId);
        pushUndo(tr("Undo New Event"), { { added, nullptr, added } });
        return true;
    }

    const EventFile *file = m_store->event(key);
    if (!file)
        return false;
    std::shared_ptr<EventFile> before = file->clone();
    std::unique_ptr<EventFile> changed = file->clone();
    QList<Change> changes;

    if (!file->isRecurring() || recurrenceKey.isEmpty() || scope == EventFile::AllEvents
            || (scope == EventFile::FutureEvents && file->isFirstOccurrence(recurrenceKey))) {
        const EventData series = seriesData(*file, recurrenceKey, data);
        if (!recurrenceKey.isEmpty())
            changed->resetOccurrence(recurrenceKey);
        changed->setData(series);
    } else if (scope == EventFile::ThisEvent) {
        changed->setOccurrenceData(recurrenceKey, data);
    } else {
        std::unique_ptr<EventFile> rest = changed->splitAt(recurrenceKey, data);
        if (!rest)
            return false;
        const QString added = m_store->addEvent(calendarId, std::move(rest));
        if (added.isEmpty())
            return false;
        changes.append({ added, nullptr, added });
        // The earlier part stays in its calendar.
        calendarId = CalendarStore::calendarOf(key);
    }

    QString updated = m_store->updateEvent(key, std::move(changed));
    if (updated.isEmpty())
        return false;
    if (CalendarStore::calendarOf(updated) != calendarId)
        updated = m_store->moveEvent(updated, calendarId);
    changes.prepend({ key, before, updated });
    pushUndo(tr("Undo Edit"), changes);
    return true;
}

bool Application::removeEvent(const QString &key, const QString &recurrenceKey, int scope)
{
    const EventFile *file = m_store->event(key);
    if (!file)
        return false;
    std::shared_ptr<EventFile> before = file->clone();

    if (file->isRecurring() && !recurrenceKey.isEmpty() && scope != EventFile::AllEvents) {
        std::unique_ptr<EventFile> changed = file->clone();
        bool left = true;
        if (scope == EventFile::ThisEvent)
            changed->excludeOccurrence(recurrenceKey);
        else
            left = changed->endBefore(recurrenceKey);
        if (left) {
            if (m_store->updateEvent(key, std::move(changed)).isEmpty())
                return false;
            pushUndo(tr("Undo Delete"), { { key, before, key } });
            return true;
        }
    }

    if (!m_store->removeEvent(key))
        return false;
    pushUndo(tr("Undo Delete"), { { key, before, QString() } });
    return true;
}

bool Application::moveOccurrence(const QString &key, const QString &recurrenceKey, int startDays, int startMinutes,
                                 int endDays, int endMinutes, int scope)
{
    const EventFile *file = m_store->event(key);
    if (!file)
        return false;
    EventData data = file->data(recurrenceKey);
    if (data.allDay) {
        data.startDate = data.startDate.addDays(startDays);
        data.endDate = qMax(data.startDate, data.endDate.addDays(endDays));
    } else {
        data.start = data.start.addDays(startDays).addSecs(qint64(startMinutes) * 60);
        data.end = qMax(data.start, data.end.addDays(endDays).addSecs(qint64(endMinutes) * 60));
    }
    // Moving changes the times only; the rule and alarms stay as they are.
    data.repeat.frequency = Repeat::Custom;
    data.alarm = EventData::CustomAlarm;

    QVariantMap map = mapFrom(data);
    map.insert(QStringLiteral("key"), key);
    map.insert(QStringLiteral("recurrenceKey"), recurrenceKey);
    map.insert(QStringLiteral("calendarId"), CalendarStore::calendarOf(key));
    if (!saveEvent(map, scope))
        return false;
    if (!m_undo.isEmpty())
        m_undo.last().text = tr("Undo Move");
    Q_EMIT undoChanged();
    return true;
}

void Application::pushUndo(const QString &text, const QList<Change> &changes)
{
    m_undo.append({ text, changes });
    while (m_undo.size() > UndoDepth)
        m_undo.removeFirst();
    Q_EMIT undoChanged();
}

void Application::undo()
{
    if (m_undo.isEmpty())
        return;
    const UndoStep step = m_undo.takeLast();
    for (auto it = step.changes.crbegin(); it != step.changes.crend(); ++it) {
        const Change &change = *it;
        if (!change.before) {
            m_store->removeEvent(change.after);
            continue;
        }
        if (!change.after.isEmpty() && change.after != change.key)
            m_store->removeEvent(change.after);
        if (m_store->event(change.key))
            m_store->updateEvent(change.key, change.before->clone());
        else
            m_store->addEvent(CalendarStore::calendarOf(change.key), change.before->clone());
    }
    Q_EMIT undoChanged();
}

QString Application::createCalendar(const QString &name, const QColor &color)
{
    return m_store->createCalendar(name, color);
}

bool Application::updateCalendar(const QString &id, const QString &name, const QColor &color)
{
    return m_store->updateCalendar(id, name, color);
}

bool Application::removeCalendar(const QString &id)
{
    if (m_store->calendars().size() <= 1 || !m_store->removeCalendar(id))
        return false;
    // Undo would bring back events of a calendar that is gone.
    m_undo.clear();
    Q_EMIT undoChanged();
    if (m_hidden.remove(id))
        m_settings.setValue(QStringLiteral("Calendars/hidden"), QStringList(m_hidden.begin(), m_hidden.end()));
    Q_EMIT defaultCalendarChanged();
    return true;
}

void Application::setCalendarVisible(const QString &id, bool visible)
{
    if (visible == !m_hidden.contains(id))
        return;
    if (visible)
        m_hidden.remove(id);
    else
        m_hidden.insert(id);
    m_settings.setValue(QStringLiteral("Calendars/hidden"), QStringList(m_hidden.begin(), m_hidden.end()));
    ++m_revision;
    Q_EMIT revisionChanged();
}

int Application::eventCount(const QString &id) const
{
    return m_store->eventCount(id);
}

int Application::importFile(const QUrl &url, const QString &calendarId)
{
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    const int count = m_store->importData(calendarId, file.readAll());
    if (count >= 0) {
        // Undo cannot tell imported events from the ones they replaced.
        m_undo.clear();
        Q_EMIT undoChanged();
    }
    return count;
}

bool Application::exportCalendar(const QString &id, const QUrl &url) const
{
    QSaveFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(m_store->exportCalendar(id));
    return file.commit();
}

QString Application::exportFileName(const QString &id) const
{
    QString name = m_store->calendar(id).name;
    name.replace(QLatin1Char('/'), QLatin1Char('-'));
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/')
            + (name.isEmpty() ? tr("Calendar") : name) + QStringLiteral(".ics");
}

void Application::activateWindow()
{
    Q_EMIT activationRequested();
}

void Application::open(const QStringList &files, const QString &date)
{
    const QDate day = QDate::fromString(date, Qt::ISODate);
    if (day.isValid())
        Q_EMIT dateRequested(startOfDay(day));
    if (!files.isEmpty())
        Q_EMIT importRequested(files);
    Q_EMIT activationRequested();
}
