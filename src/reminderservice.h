// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef REMINDERSERVICE_H
#define REMINDERSERVICE_H

#include "reminders.h"

#include <QHash>
#include <QObject>
#include <QTimer>

class CalendarStore;

class ReminderService : public QObject
{
    Q_OBJECT

public:
    explicit ReminderService(CalendarStore *store, QObject *parent = nullptr);

private:
    void check();
    void notify(const Reminder &reminder);

    CalendarStore *m_store;
    QTimer m_timer;
    QDateTime m_checked;
    // Fired alarms, kept until a day after they went off.
    QHash<QString, QDateTime> m_fired;
    QList<Reminder> m_snoozed;
};

#endif
