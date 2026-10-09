// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "calendarstore.h"
#include "cutefishapplication.h"
#include "reminderservice.h"

#include <QCoreApplication>
#include <QDBusConnection>

int main(int argc, char *argv[])
{
    Cutefish::Application::prepare();
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("cutefish-calendar-reminders"));

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.registerService(QStringLiteral("com.cutefish.CalendarReminders")))
        return 0;

    CalendarStore store;
    ReminderService service(&store);
    store.loadAsync();
    return app.exec();
}
