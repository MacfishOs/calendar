// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "calendarstore.h"
#include "reminderservice.h"

#include <base/dbus.h>
#include <base/event_loop.h>
#include <base/i18n.h>
#include <base/locale.h>
#include <base/session_language.h>


int main()
{
    cutefish::base::Locale::setProcessLocale();
    cutefish::i18n::setDomain("cutefish-calendar");
    cutefish::base::EventLoop loop;
    cutefish::base::dbus::Connection* bus = cutefish::base::dbus::Connection::session(&loop);
    if (!bus || !bus->requestName("org.cutefish.CalendarReminders"))
        return 0;

    cutefish::base::SessionLanguage language(bus);
    CalendarStore store;
    ReminderService service(&store, bus);
    store.loadAsync();
    return loop.run();
}
