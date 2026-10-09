// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENTFILE_H
#define EVENTFILE_H

#include "occurrence.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

#include <memory>
#include <vector>

typedef struct icalcomponent_impl icalcomponent;

// How an event repeats, as far as the editor shows it. Anything the editor
// cannot show is Custom and left as it is when the event is saved.
struct Repeat
{
    enum Frequency { None, Daily, Weekdays, Weekly, Monthly, Yearly, Custom };

    Frequency frequency = None;
    // Ends after count occurrences, or on the day until; never when neither is set.
    int count = 0;
    QDate until;

    bool operator==(const Repeat &other) const
    {
        return frequency == other.frequency && count == other.count && until == other.until;
    }
};

// The fields the editor changes, for one event or one occurrence of it.
struct EventData
{
    enum Alarm { NoAlarm, OffsetAlarm, CustomAlarm };

    QString summary;
    QString location;
    QString description;
    bool allDay = false;
    QDateTime start;
    QDateTime end;
    // All-day events; the last day is included.
    QDate startDate;
    QDate endDate;
    Repeat repeat;
    Alarm alarm = NoAlarm;
    // Minutes from the start; negative is before it.
    int alarmOffset = 0;
};

// One event and the instances of it that were changed on their own. Fields the
// editor does not show, from imported or synced files, are kept.
class EventFile
{
public:
    enum Scope { ThisEvent, FutureEvents, AllEvents };

    ~EventFile();

    static std::unique_ptr<EventFile> fromData(const QByteArray &data);
    // One file per event in a calendar of many, as an import needs them.
    static std::vector<std::unique_ptr<EventFile>> split(const QByteArray &data);
    static std::unique_ptr<EventFile> create(const EventData &data);

    std::unique_ptr<EventFile> clone() const;
    QByteArray toData() const;
    static QByteArray join(const QList<const EventFile *> &files);

    QString uid() const;
    QString summary() const;
    QString location() const;
    QString description() const;
    bool isRecurring() const;

    // `to` is excluded.
    void expand(QDate from, QDate to, QList<Occurrence> *out) const;
    // The first occurrence starting at or after `after`; for a repeating
    // event the search stops `within` days on.
    bool nextOccurrence(const QDateTime &after, int within, Occurrence *out) const;
    QList<QPair<QDateTime, Occurrence>> alarms(const QDateTime &from, const QDateTime &to) const;

    EventData data(const QString &recurrenceKey = QString()) const;
    // Sets the fields of the whole event; a changed start moves every occurrence.
    void setData(const EventData &data);
    void setOccurrenceData(const QString &recurrenceKey, const EventData &data);
    void resetOccurrence(const QString &recurrenceKey);
    void excludeOccurrence(const QString &recurrenceKey);
    // Ends the series before this occurrence. False when nothing is left.
    bool endBefore(const QString &recurrenceKey);
    // The returned event, with a new UID, takes this occurrence and the later ones;
    // null for the first occurrence, which needs setData instead.
    std::unique_ptr<EventFile> splitAt(const QString &recurrenceKey, const EventData &data);
    bool isFirstOccurrence(const QString &recurrenceKey) const;

private:
    explicit EventFile(icalcomponent *calendar);

    icalcomponent *master() const;
    icalcomponent *override(const QString &recurrenceKey) const;
    void touch(icalcomponent *event) const;

    icalcomponent *m_calendar = nullptr;
};

#endif
