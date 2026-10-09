// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "timezones.h"

#include <QHash>
#include <QMutex>

static QTimeZone lookup(const QByteArray &tzid)
{
    const QTimeZone exact(tzid);
    if (exact.isValid())
        return exact;

    // Vendor prefixes keep the IANA id at the end, as one to three components.
    const QList<QByteArray> parts = tzid.split('/');
    for (int count = qMin<int>(3, parts.size() - 1); count >= 1; --count) {
        const QByteArray suffix = parts.mid(parts.size() - count).join('/');
        const QTimeZone zone(suffix);
        if (zone.isValid())
            return zone;
    }

    const QByteArray windows = QTimeZone::windowsIdToDefaultIanaId(tzid);
    if (!windows.isEmpty())
        return QTimeZone(windows);
    return {};
}

QTimeZone TimeZones::fromTzid(const QByteArray &tzid)
{
    // Files are parsed on a worker thread as well.
    static QMutex mutex;
    static QHash<QByteArray, QTimeZone> cache;

    QMutexLocker locker(&mutex);
    auto it = cache.constFind(tzid);
    if (it == cache.constEnd())
        it = cache.insert(tzid, lookup(tzid));
    return *it;
}

QTimeZone TimeZones::system()
{
    const QTimeZone zone(QTimeZone::systemTimeZoneId());
    return zone.isValid() ? zone : QTimeZone(QTimeZone::LocalTime);
}
