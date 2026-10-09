// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "calendarstore.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>

static QString readSmallFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.read(4096)).trimmed();
}

static bool writeSmallFile(const QString &path, const QString &text)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(text.toUtf8() + '\n');
    return file.commit();
}

CalendarStore::CalendarStore(const QString &root, QObject *parent)
    : QObject(parent)
    , m_root(root)
{
    m_rescan.setSingleShot(true);
    m_rescan.setInterval(250);
    connect(&m_rescan, &QTimer::timeout, this, [this] {
        apply(scan(m_root, m_events));
    });
}

CalendarStore::~CalendarStore() = default;

QString CalendarStore::defaultRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/cutefish-calendar/calendars");
}

CalendarStore::Entry CalendarStore::makeEntry(std::shared_ptr<const EventFile> file)
{
    Entry entry;
    entry.recurring = file->isRecurring();
    if (!entry.recurring) {
        QList<Occurrence> occurrences;
        file->expand(QDate(1, 1, 1), QDate(9999, 12, 31), &occurrences);
        if (!occurrences.isEmpty()) {
            entry.firstDay = occurrences.first().firstDay;
            entry.lastDay = occurrences.first().lastDay;
        }
    }
    entry.file = std::move(file);
    return entry;
}

CalendarStore::Snapshot CalendarStore::scan(const QString &root, const QHash<QString, Entry> &known)
{
    Snapshot snapshot;
    const QDir rootDir(root);
    const QStringList ids = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &id : ids) {
        const QString path = rootDir.filePath(id);
        CalendarInfo info;
        info.id = id;
        info.name = readSmallFile(path + QStringLiteral("/displayname"));
        if (info.name.isEmpty())
            info.name = id;
        info.color = QColor(readSmallFile(path + QStringLiteral("/color")).left(7));
        if (!info.color.isValid())
            info.color = QColor(0x3c, 0x81, 0xf6);
        snapshot.calendars.insert(id, info);

        const QFileInfoList files = QDir(path).entryInfoList({ QStringLiteral("*.ics") }, QDir::Files);
        for (const QFileInfo &fileInfo : files) {
            const QString key = id + QLatin1Char('/') + fileInfo.fileName();
            const auto it = known.constFind(key);
            if (it != known.constEnd() && it->modified == fileInfo.lastModified() && it->size == fileInfo.size()) {
                snapshot.events.insert(key, *it);
                continue;
            }
            QFile file(fileInfo.filePath());
            if (!file.open(QIODevice::ReadOnly))
                continue;
            std::shared_ptr<const EventFile> event = EventFile::fromData(file.readAll());
            if (!event)
                continue;
            Entry entry = makeEntry(std::move(event));
            entry.modified = fileInfo.lastModified();
            entry.size = fileInfo.size();
            snapshot.events.insert(key, entry);
        }
    }
    return snapshot;
}

void CalendarStore::load()
{
    apply(scan(m_root, {}));
}

void CalendarStore::loadAsync()
{
    auto *watcher = new QFutureWatcher<Snapshot>(this);
    connect(watcher, &QFutureWatcher<Snapshot>::finished, this, [this, watcher] {
        apply(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&CalendarStore::scan, m_root, QHash<QString, Entry>()));
}

void CalendarStore::apply(Snapshot snapshot)
{
    m_calendars = std::move(snapshot.calendars);
    m_events = std::move(snapshot.events);
    watch();
    if (!m_ready) {
        m_ready = true;
        Q_EMIT readyChanged();
    }
    Q_EMIT changed();
}

void CalendarStore::watch()
{
    if (!m_watcher) {
        QDir().mkpath(m_root);
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
            m_rescan.start();
        });
    }
    QStringList paths { m_root };
    for (const QString &id : m_calendars.keys())
        paths.append(m_root + QLatin1Char('/') + id);
    const QStringList watched = m_watcher->directories();
    for (const QString &path : watched) {
        if (!paths.contains(path))
            m_watcher->removePath(path);
    }
    for (const QString &path : std::as_const(paths)) {
        if (!watched.contains(path))
            m_watcher->addPath(path);
    }
}

QList<CalendarInfo> CalendarStore::calendars() const
{
    QList<CalendarInfo> list = m_calendars.values();
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(list.begin(), list.end(), [&collator](const CalendarInfo &a, const CalendarInfo &b) {
        return collator.compare(a.name, b.name) < 0;
    });
    return list;
}

QString CalendarStore::createCalendar(const QString &name, const QColor &color)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString path = m_root + QLatin1Char('/') + id;
    if (!QDir().mkpath(path))
        return {};
    CalendarInfo info { id, name, color };
    writeSmallFile(path + QStringLiteral("/displayname"), name);
    writeSmallFile(path + QStringLiteral("/color"), color.name(QColor::HexRgb).toUpper());
    m_calendars.insert(id, info);
    watch();
    Q_EMIT changed();
    return id;
}

bool CalendarStore::updateCalendar(const QString &id, const QString &name, const QColor &color)
{
    if (!m_calendars.contains(id))
        return false;
    const QString path = m_root + QLatin1Char('/') + id;
    if (!writeSmallFile(path + QStringLiteral("/displayname"), name)
            || !writeSmallFile(path + QStringLiteral("/color"), color.name(QColor::HexRgb).toUpper()))
        return false;
    m_calendars[id] = { id, name, color };
    Q_EMIT changed();
    return true;
}

bool CalendarStore::removeCalendar(const QString &id)
{
    if (!m_calendars.contains(id))
        return false;
    const QString path = m_root + QLatin1Char('/') + id;
    if (!QFile::moveToTrash(path) && !QDir(path).removeRecursively())
        return false;
    m_calendars.remove(id);
    m_events.removeIf([&id](const QHash<QString, Entry>::iterator it) {
        return calendarOf(it.key()) == id;
    });
    watch();
    Q_EMIT changed();
    return true;
}

int CalendarStore::eventCount(const QString &calendarId) const
{
    int count = 0;
    for (auto it = m_events.constBegin(); it != m_events.constEnd(); ++it) {
        if (calendarOf(it.key()) == calendarId)
            ++count;
    }
    return count;
}

QList<Occurrence> CalendarStore::occurrences(QDate from, QDate to, const QSet<QString> &hiddenCalendars) const
{
    QList<Occurrence> result;
    for (auto it = m_events.constBegin(); it != m_events.constEnd(); ++it) {
        const QString calendarId = calendarOf(it.key());
        if (hiddenCalendars.contains(calendarId) || !m_calendars.contains(calendarId))
            continue;
        const Entry &entry = it.value();
        if (!entry.recurring && (!entry.firstDay.isValid() || entry.firstDay >= to || entry.lastDay < from))
            continue;
        const qsizetype start = result.size();
        entry.file->expand(from, to, &result);
        for (qsizetype i = start; i < result.size(); ++i) {
            result[i].key = it.key();
            result[i].calendarId = calendarId;
        }
    }
    return result;
}

const EventFile *CalendarStore::event(const QString &key) const
{
    const auto it = m_events.constFind(key);
    return it == m_events.constEnd() ? nullptr : it->file.get();
}

QString CalendarStore::calendarOf(const QString &key)
{
    return key.section(QLatin1Char('/'), 0, 0);
}

QString CalendarStore::pathOf(const QString &key) const
{
    return m_root + QLatin1Char('/') + key;
}

QString CalendarStore::fileNameFor(const QString &calendarId, const QString &uid) const
{
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9@._-]{1,96}$"));
    QString base = safe.match(uid).hasMatch() ? uid
            : QString::fromLatin1(QCryptographicHash::hash(uid.toUtf8(), QCryptographicHash::Sha1).toHex());
    QString name = base + QStringLiteral(".ics");
    for (int i = 2; m_events.contains(calendarId + QLatin1Char('/') + name); ++i)
        name = base + QLatin1Char('-') + QString::number(i) + QStringLiteral(".ics");
    return name;
}

bool CalendarStore::write(const QString &key, const EventFile &file)
{
    QSaveFile out(pathOf(key));
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(file.toData());
    if (!out.commit())
        return false;
    return true;
}

QString CalendarStore::addEvent(const QString &calendarId, std::unique_ptr<EventFile> file)
{
    if (!m_calendars.contains(calendarId) || !file)
        return {};
    const QString key = calendarId + QLatin1Char('/') + fileNameFor(calendarId, file->uid());
    if (!write(key, *file))
        return {};
    Entry entry = makeEntry(std::move(file));
    const QFileInfo info(pathOf(key));
    entry.modified = info.lastModified();
    entry.size = info.size();
    m_events.insert(key, entry);
    Q_EMIT changed();
    return key;
}

QString CalendarStore::updateEvent(const QString &key, std::unique_ptr<EventFile> file)
{
    if (!m_events.contains(key) || !file || !write(key, *file))
        return {};
    Entry entry = makeEntry(std::move(file));
    const QFileInfo info(pathOf(key));
    entry.modified = info.lastModified();
    entry.size = info.size();
    m_events.insert(key, entry);
    Q_EMIT changed();
    return key;
}

QString CalendarStore::moveEvent(const QString &key, const QString &calendarId)
{
    const EventFile *file = event(key);
    if (!file || !m_calendars.contains(calendarId))
        return {};
    if (calendarOf(key) == calendarId)
        return key;
    const QString moved = addEvent(calendarId, file->clone());
    if (moved.isEmpty())
        return {};
    removeEvent(key);
    return moved;
}

bool CalendarStore::removeEvent(const QString &key)
{
    if (!m_events.contains(key))
        return false;
    if (!QFile::remove(pathOf(key)) && QFile::exists(pathOf(key)))
        return false;
    m_events.remove(key);
    Q_EMIT changed();
    return true;
}

int CalendarStore::importData(const QString &calendarId, const QByteArray &data)
{
    if (!m_calendars.contains(calendarId))
        return -1;
    std::vector<std::unique_ptr<EventFile>> files = EventFile::split(data);
    if (files.empty())
        return -1;

    QHash<QString, QString> byUid;
    for (auto it = m_events.constBegin(); it != m_events.constEnd(); ++it) {
        if (calendarOf(it.key()) == calendarId)
            byUid.insert(it->file->uid(), it.key());
    }

    int count = 0;
    {
        // One change for the whole file.
        const QSignalBlocker blocker(this);
        for (std::unique_ptr<EventFile> &file : files) {
            const QString existing = byUid.value(file->uid());
            const QString key = existing.isEmpty() ? addEvent(calendarId, std::move(file))
                                                   : updateEvent(existing, std::move(file));
            if (!key.isEmpty())
                ++count;
        }
    }
    Q_EMIT changed();
    return count;
}

QByteArray CalendarStore::exportCalendar(const QString &id) const
{
    QList<const EventFile *> files;
    for (auto it = m_events.constBegin(); it != m_events.constEnd(); ++it) {
        if (calendarOf(it.key()) == id)
            files.append(it->file.get());
    }
    return EventFile::join(files);
}
