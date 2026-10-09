// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TIMEZONES_H
#define TIMEZONES_H

#include <QByteArray>
#include <QTimeZone>

namespace TimeZones
{
// An IANA id, one behind a vendor prefix ("/freeassociation.sourceforge.net/
// Europe/Berlin") or a Windows name; invalid when none is known.
QTimeZone fromTzid(const QByteArray &tzid);

QTimeZone system();
}

#endif
