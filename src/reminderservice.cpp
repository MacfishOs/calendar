// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "reminderservice.h"
#include "applicationlauncher.h"
#include "calendarstore.h"
#include "cutefishnotification.h"

#include <QLocale>

// A snoozed reminder comes back after this long.
static const int SnoozeMinutes = 10;
// Alarms just before the service started, as at login, still show.
static const int CatchUpMinutes = 10;
// Checked at least this often, so a changed clock is noticed.
static const int LongestWaitMs = 60 * 60 * 1000;

ReminderService::ReminderService(CalendarStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ReminderService::check);
    connect(m_store, &CalendarStore::changed, this, &ReminderService::check);
    m_checked = QDateTime::currentDateTime().addSecs(-CatchUpMinutes * 60);
}

void ReminderService::check()
{
    if (!m_store->isReady())
        return;

    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime until = now.addSecs(1);
    QList<Reminder> due = m_checked < until ? Reminders::due(*m_store, m_checked, until) : QList<Reminder>();
    for (auto it = m_snoozed.begin(); it != m_snoozed.end();) {
        if (it->time <= now) {
            due.append(*it);
            it = m_snoozed.erase(it);
        } else {
            ++it;
        }
    }

    for (const Reminder &reminder : std::as_const(due)) {
        const QString id = reminder.id();
        if (m_fired.contains(id))
            continue;
        m_fired.insert(id, reminder.time);
        // Missed while asleep and over by now: no longer worth a notification.
        const Occurrence &occurrence = reminder.occurrence;
        const QDateTime end = occurrence.allDay ? QDateTime(occurrence.lastDay.addDays(1), QTime(0, 0)) : occurrence.end;
        if (end <= now && reminder.time < now.addSecs(-CatchUpMinutes * 60))
            continue;
        notify(reminder);
    }
    m_checked = until;
    m_fired.removeIf([&now](const QHash<QString, QDateTime>::iterator it) {
        return it.value() < now.addDays(-1);
    });

    QDateTime next = now.addMSecs(LongestWaitMs);
    const QList<Reminder> upcoming = Reminders::due(*m_store, until, next);
    if (!upcoming.isEmpty())
        next = qMin(next, upcoming.first().time);
    for (const Reminder &reminder : std::as_const(m_snoozed))
        next = qMin(next, reminder.time);
    m_timer.start(int(qBound<qint64>(1000, now.msecsTo(next), qint64(LongestWaitMs))));
}

static QString timeText(const Occurrence &occurrence)
{
    const QLocale locale;
    const QDate today = QDate::currentDate();
    if (occurrence.allDay) {
        if (occurrence.firstDay == today)
            return ReminderService::tr("Today, all day");
        if (occurrence.firstDay == today.addDays(1))
            return ReminderService::tr("Tomorrow, all day");
        return locale.toString(occurrence.firstDay, QLocale::LongFormat);
    }

    QString range = locale.toString(occurrence.start.time(), QLocale::ShortFormat);
    if (occurrence.end > occurrence.start)
        range += QStringLiteral(" – ") + locale.toString(occurrence.end.time(), QLocale::ShortFormat);
    if (occurrence.start.date() == today)
        return range;
    if (occurrence.start.date() == today.addDays(1))
        return ReminderService::tr("Tomorrow, %1").arg(range);
    return locale.toString(occurrence.start.date(), QLocale::LongFormat) + QStringLiteral(", ") + range;
}

void ReminderService::notify(const Reminder &reminder)
{
    const Occurrence &occurrence = reminder.occurrence;
    auto *notification = new Cutefish::Notification(this);
    notification->setDesktopEntry(QStringLiteral("cutefish-calendar"));
    notification->setIconName(QStringLiteral("office-calendar"));
    notification->setSummary(occurrence.summary.isEmpty() ? tr("New Event") : occurrence.summary);
    QString body = timeText(occurrence);
    if (!occurrence.location.isEmpty())
        body += QLatin1Char('\n') + occurrence.location;
    notification->setBody(body.toHtmlEscaped());
    notification->setTimeout(0);
    notification->addAction(QStringLiteral("default"));
    notification->addAction(QStringLiteral("snooze"), tr("Snooze %1 Minutes").arg(SnoozeMinutes));
    notification->setAutoDelete(true);

    const QDate date = occurrence.allDay ? occurrence.firstDay : occurrence.start.date();
    connect(notification, &Cutefish::Notification::actionInvoked, this, [this, reminder, date](const QString &key) {
        if (key == QLatin1String("snooze")) {
            Reminder later = reminder;
            later.time = QDateTime::currentDateTime().addSecs(SnoozeMinutes * 60);
            m_snoozed.append(later);
            check();
        } else {
            ApplicationLauncher::startDetached({ QStringLiteral("cutefish-calendar"), QStringLiteral("--date"),
                                                 date.toString(Qt::ISODate) },
                                               QString(), QStringLiteral("cutefish-calendar"));
        }
    });
    notification->show();
}
