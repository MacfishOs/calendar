// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "datetime.h"

#include <base/signal.h>
#include <ui/scroll_view.h>

#include <memory>
#include <vector>

namespace cutefish::ui {
class Column;
class Label;
}

class CalendarRow;
class CalendarWindow;
class MiniMonth;

// The month at a glance and the calendars, each shown or hidden.
class Sidebar : public cutefish::ui::ScrollView
{
public:
    explicit Sidebar(CalendarWindow* window);
    ~Sidebar() override;

protected:
    void layout() override;

private:
    class Card;

    void updateMonth();
    void updateCalendars();

    CalendarWindow* m_window;
    // The month the small calendar shows; it follows the main view until
    // its own arrows move it.
    Date m_shownMonth;
    cutefish::ui::Label* m_monthLabel;
    cutefish::ui::Label* m_heading;
    MiniMonth* m_miniMonth;
    Card* m_calendarList;
    std::vector<CalendarRow*> m_rows;
    bool m_updatePosted = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::vector<cutefish::base::Subscription> m_subscriptions;
};
