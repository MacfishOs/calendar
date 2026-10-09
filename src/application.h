// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef APPLICATION_H
#define APPLICATION_H

#include "calendarstore.h"

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVariant>

// What the QML sees as App: the calendars, their events laid out for each
// view, and the edits made to them.
class Application : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.cutefish.Calendar")
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    // Bumped on every change to calendars or events; layouts depend on it.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY revisionChanged)
    Q_PROPERTY(QString defaultCalendar READ defaultCalendar WRITE setDefaultCalendar NOTIFY defaultCalendarChanged)
    Q_PROPERTY(QDateTime today READ today NOTIFY todayChanged)
    // 0 follows the language; 1 to 7 is Monday to Sunday.
    Q_PROPERTY(int weekStart READ weekStart WRITE setWeekStart NOTIFY weekStartChanged)
    Q_PROPERTY(int firstDayOfWeek READ firstDayOfWeek NOTIFY weekStartChanged)
    Q_PROPERTY(bool showWeekNumbers READ showWeekNumbers WRITE setShowWeekNumbers NOTIFY showWeekNumbersChanged)
    Q_PROPERTY(bool use24Hour READ use24Hour CONSTANT)
    // Days of the weekend as the language counts them, 0 for Sunday to 6 for Saturday.
    Q_PROPERTY(QVariantList weekendDays READ weekendDays CONSTANT)
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged)
    // Whether the sidebar is wanted; a narrow window hides it anyway.
    Q_PROPERTY(bool sidebarVisible READ sidebarVisible WRITE setSidebarVisible NOTIFY sidebarVisibleChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)

public:
    explicit Application(CalendarStore *store, QObject *parent = nullptr);

    bool isReady() const { return m_store->isReady(); }
    int revision() const { return m_revision; }
    QVariantList calendars() const;
    QString defaultCalendar() const;
    void setDefaultCalendar(const QString &id);
    QDateTime today() const { return QDateTime(m_today, QTime(0, 0)); }
    int weekStart() const { return m_weekStart; }
    void setWeekStart(int day);
    int firstDayOfWeek() const;
    bool showWeekNumbers() const { return m_showWeekNumbers; }
    void setShowWeekNumbers(bool show);
    bool use24Hour() const;
    QVariantList weekendDays() const;
    QString view() const { return m_view; }
    void setView(const QString &view);
    bool sidebarVisible() const { return m_sidebarVisible; }
    void setSidebarVisible(bool visible);
    bool canUndo() const { return !m_undo.isEmpty(); }
    QString undoText() const { return m_undo.isEmpty() ? QString() : m_undo.last().text; }

    Q_INVOKABLE QVariantMap monthLayout(const QDateTime &first, int rows, int lanes) const;
    Q_INVOKABLE QVariantMap dayLayout(const QDateTime &first, int days, int lanes) const;
    Q_INVOKABLE QVariantList busyDays(const QDateTime &first, int days) const;
    Q_INVOKABLE QVariantList search(const QString &text) const;
    Q_INVOKABLE int weekNumber(const QDateTime &rowStart) const;

    Q_INVOKABLE QVariantMap eventData(const QString &key, const QString &recurrenceKey) const;
    Q_INVOKABLE QVariantMap newEventData(const QDateTime &start, const QDateTime &end, bool allDay) const;
    Q_INVOKABLE bool isFirstOccurrence(const QString &key, const QString &recurrenceKey) const;
    // Scope is EventFile::Scope: 0 this event, 1 this and later ones, 2 all.
    Q_INVOKABLE bool saveEvent(const QVariantMap &data, int scope);
    Q_INVOKABLE bool removeEvent(const QString &key, const QString &recurrenceKey, int scope);
    // Moves an occurrence's start and end by whole days and minutes, as a drag does.
    Q_INVOKABLE bool moveOccurrence(const QString &key, const QString &recurrenceKey, int startDays, int startMinutes,
                                    int endDays, int endMinutes, int scope);
    Q_INVOKABLE void undo();

    Q_INVOKABLE QString createCalendar(const QString &name, const QColor &color);
    Q_INVOKABLE bool updateCalendar(const QString &id, const QString &name, const QColor &color);
    Q_INVOKABLE bool removeCalendar(const QString &id);
    Q_INVOKABLE void setCalendarVisible(const QString &id, bool visible);
    Q_INVOKABLE int eventCount(const QString &id) const;
    // How many events the file had, -1 when it was not a calendar.
    Q_INVOKABLE int importFile(const QUrl &url, const QString &calendarId);
    Q_INVOKABLE bool exportCalendar(const QString &id, const QUrl &url) const;
    Q_INVOKABLE QString exportFileName(const QString &id) const;

public Q_SLOTS:
    Q_SCRIPTABLE void activateWindow();
    Q_SCRIPTABLE void open(const QStringList &files, const QString &date);

Q_SIGNALS:
    void readyChanged();
    void revisionChanged();
    void defaultCalendarChanged();
    void todayChanged();
    void weekStartChanged();
    void showWeekNumbersChanged();
    void viewChanged();
    void sidebarVisibleChanged();
    void undoChanged();
    void activationRequested();
    void dateRequested(const QDateTime &date);
    void importRequested(const QStringList &files);

private:
    struct Change
    {
        QString key;
        // The file as it was; null when the change created it.
        std::shared_ptr<EventFile> before;
        // Where it is now; empty when the change removed it.
        QString after;
    };
    struct UndoStep
    {
        QString text;
        QList<Change> changes;
    };

    QVariantMap occurrenceMap(const Occurrence &occurrence) const;
    QSet<QString> hiddenCalendars() const;
    void pushUndo(const QString &text, const QList<Change> &changes);
    void ensureCalendar();

    CalendarStore *m_store;
    QSettings m_settings;
    int m_revision = 0;
    QDate m_today;
    QTimer m_dayTimer;
    int m_weekStart = 0;
    bool m_showWeekNumbers = false;
    QString m_view;
    bool m_sidebarVisible = true;
    QSet<QString> m_hidden;
    QList<UndoStep> m_undo;
};

#endif
