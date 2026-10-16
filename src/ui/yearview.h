// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "datetime.h"

#include <base/signal.h>
#include <ui/view.h>

#include <string>
#include <vector>

class CalendarWindow;
class MiniMonth;
class MonthTitle;

// The twelve months of the year, days with events marked by a dot.
class YearView : public cutefish::ui::View
{
public:
    explicit YearView(CalendarWindow* window);
    ~YearView() override;

    void setLive(bool live);
    void refresh(bool force = false);

protected:
    void layout() override;

private:
    int year() const;
    Date busyStart() const;

    CalendarWindow* m_window;
    bool m_live = false;
    std::string m_busyKey;
    std::vector<bool> m_busy;
    std::vector<MonthTitle*> m_titles;
    std::vector<MiniMonth*> m_months;
    std::vector<cutefish::base::Subscription> m_subscriptions;
};
