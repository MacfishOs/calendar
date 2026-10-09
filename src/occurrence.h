// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef OCCURRENCE_H
#define OCCURRENCE_H

#include <QDateTime>
#include <QString>

// One appearance of an event on the calendar: the event itself, or one
// instance of a repeating one.
struct Occurrence
{
    // "<calendar id>/<file name>", the event's file in the store.
    QString key;
    // Which instance of a repeating event this is; empty for a single event.
    QString recurrenceKey;
    QString summary;
    QString location;
    bool allDay = false;
    bool recurring = false;
    QDateTime start;
    QDateTime end;
    // The local days it touches, last one included.
    QDate firstDay;
    QDate lastDay;
    QString calendarId;
};

#endif
