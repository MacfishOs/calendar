// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "calendarstore.h"
#include "reminderservice.h"

#include <base/dbus.h>
#include <base/event_loop.h>

int main()
{
    cutefish::base::EventLoop loop;
    cutefish::base::dbus::Connection* bus = cutefish::base::dbus::Connection::session(&loop);
    if (!bus || !bus->requestName("com.cutefish.CalendarReminders"))
        return 0;

    CalendarStore store;
    ReminderService service(&store, bus);
    store.loadAsync();
    return loop.run();
}
