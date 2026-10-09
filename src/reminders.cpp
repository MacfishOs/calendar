// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "reminders.h"
#include "calendarstore.h"

#include <algorithm>

QString Reminder::id() const
{
    return occurrence.key + QLatin1Char('#') + occurrence.recurrenceKey + QLatin1Char('@')
            + QString::number(time.toSecsSinceEpoch());
}

QList<Reminder> Reminders::due(const CalendarStore &store, const QDateTime &from, const QDateTime &to)
{
    QList<Reminder> reminders;
    const QStringList keys = store.keys();
    for (const QString &key : keys) {
        const EventFile *file = store.event(key);
        if (!file)
            continue;
        const auto alarms = file->alarms(from, to);
        for (const auto &alarm : alarms) {
            Reminder reminder { alarm.first, alarm.second };
            reminder.occurrence.key = key;
            reminder.occurrence.calendarId = CalendarStore::calendarOf(key);
            reminders.append(reminder);
        }
    }
    std::sort(reminders.begin(), reminders.end(), [](const Reminder &a, const Reminder &b) {
        return a.time < b.time;
    });
    return reminders;
}
