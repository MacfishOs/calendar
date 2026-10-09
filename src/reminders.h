// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef REMINDERS_H
#define REMINDERS_H

#include "occurrence.h"

#include <QList>

class CalendarStore;

struct Reminder
{
    QDateTime time;
    Occurrence occurrence;

    // Names this alarm of this occurrence, to fire it once.
    QString id() const;
};

namespace Reminders
{
QList<Reminder> due(const CalendarStore &store, const QDateTime &from, const QDateTime &to);
}

#endif
