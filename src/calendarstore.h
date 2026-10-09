// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CALENDARSTORE_H
#define CALENDARSTORE_H

#include "eventfile.h"

#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <memory>

class QFileSystemWatcher;

struct CalendarInfo
{
    QString id;
    QString name;
    QColor color;
};

// Calendars as vdir directories: one iCalendar file per event, name and colour
// in `displayname` and `color`. Changes made by others on disk are picked up.
class CalendarStore : public QObject
{
    Q_OBJECT

public:
    explicit CalendarStore(const QString &root = defaultRoot(), QObject *parent = nullptr);
    ~CalendarStore() override;

    static QString defaultRoot();

    QString root() const { return m_root; }
    void load();
    void loadAsync();
    bool isReady() const { return m_ready; }

    QList<CalendarInfo> calendars() const;
    bool hasCalendar(const QString &id) const { return m_calendars.contains(id); }
    CalendarInfo calendar(const QString &id) const { return m_calendars.value(id); }
    QString createCalendar(const QString &name, const QColor &color);
    bool updateCalendar(const QString &id, const QString &name, const QColor &color);
    // Moves the calendar's directory to the trash.
    bool removeCalendar(const QString &id);
    int eventCount(const QString &calendarId) const;

    QList<Occurrence> occurrences(QDate from, QDate to, const QSet<QString> &hiddenCalendars = {}) const;
    QStringList keys() const { return m_events.keys(); }
    const EventFile *event(const QString &key) const;
    static QString calendarOf(const QString &key);

    QString addEvent(const QString &calendarId, std::unique_ptr<EventFile> file);
    QString updateEvent(const QString &key, std::unique_ptr<EventFile> file);
    QString moveEvent(const QString &key, const QString &calendarId);
    bool removeEvent(const QString &key);

    // Replaces events with the same UID; -1 when the data holds none.
    int importData(const QString &calendarId, const QByteArray &data);
    QByteArray exportCalendar(const QString &id) const;

Q_SIGNALS:
    void readyChanged();
    void changed();

private:
    struct Entry
    {
        std::shared_ptr<const EventFile> file;
        QDateTime modified;
        qint64 size = 0;
        // Days a single event touches; repeating events are always expanded.
        QDate firstDay;
        QDate lastDay;
        bool recurring = false;
    };
    struct Snapshot
    {
        QHash<QString, CalendarInfo> calendars;
        QHash<QString, Entry> events;
    };

    static Snapshot scan(const QString &root, const QHash<QString, Entry> &known);
    static Entry makeEntry(std::shared_ptr<const EventFile> file);
    void apply(Snapshot snapshot);
    void watch();
    bool write(const QString &key, const EventFile &file);
    QString fileNameFor(const QString &calendarId, const QString &uid) const;
    QString pathOf(const QString &key) const;

    QString m_root;
    bool m_ready = false;
    QHash<QString, CalendarInfo> m_calendars;
    QHash<QString, Entry> m_events;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_rescan;
};

#endif
