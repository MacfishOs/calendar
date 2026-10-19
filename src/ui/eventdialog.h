// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "calendarmodel.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cutefish::ui {
class ComboBox;
class DialogButton;
class DialogWindow;
class Label;
class Switch;
class TextArea;
class TextField;
class View;
class Window;
}

class DateButton;
class FormCard;

// The editor of an event: its title and place, when it is, how it repeats,
// its alert, calendar and notes.
class EventDialog
{
public:
    EventDialog(cutefish::ui::Window* parent, CalendarModel* model, EventForm event);
    ~EventDialog();

    const EventForm& event() const { return m_event; }
    bool isNew() const { return m_event.key.empty(); }
    cutefish::ui::Window* window() const;

    void show();
    void close();
    void save();
    // Made again in the session's new language, with what was entered so far.
    void retranslate();

    // A typed time in minutes of the day, or the fallback when it reads as none.
    static int parseTime(const std::string& text, int fallback);

    std::function<void(const EventForm& form)> onSaveRequested;
    std::function<void()> onDeleteRequested;
    std::function<void()> onClosed;

private:
    struct Alarm
    {
        std::string text;
        int alarm;
        int offset;
    };

    void buildContent();
    DateTime start() const;
    DateTime end() const;
    bool isValid() const { return end() >= start(); }
    std::vector<Alarm> alarmChoices() const;
    int alarmIndexFor(int alarm, int offset) const;
    // The event keeps its length when its start moves.
    void setStart(Date date, int minute);
    void update();
    void setUpTimeBox(cutefish::ui::ComboBox* box, std::function<int()> minute, std::function<void(int)> changed);
    static void showTime(cutefish::ui::ComboBox* box, int minute);

    CalendarModel* m_model;
    EventForm m_event;
    std::unique_ptr<cutefish::ui::DialogWindow> m_dialog;

    bool m_allDay = false;
    Date m_startDate;
    Date m_endDate;
    int m_startMinute = 0;
    int m_endMinute = 0;
    int m_repeat = 0;
    int m_repeatEnd = 0;
    Date m_repeatUntil;
    int m_repeatCount = 10;
    int m_alarmIndex = 0;
    int m_shownStartMinute = -1;
    int m_shownEndMinute = -1;
    std::vector<int> m_repeatValues;

    cutefish::ui::TextField* m_title = nullptr;
    cutefish::ui::TextField* m_location = nullptr;
    cutefish::ui::TextArea* m_notes = nullptr;
    cutefish::ui::Switch* m_allDaySwitch = nullptr;
    DateButton* m_startDateButton = nullptr;
    DateButton* m_endDateButton = nullptr;
    DateButton* m_untilButton = nullptr;
    cutefish::ui::ComboBox* m_startTime = nullptr;
    cutefish::ui::ComboBox* m_endTime = nullptr;
    cutefish::ui::ComboBox* m_repeatBox = nullptr;
    cutefish::ui::ComboBox* m_repeatEndBox = nullptr;
    cutefish::ui::TextField* m_countField = nullptr;
    cutefish::ui::Label* m_timesLabel = nullptr;
    cutefish::ui::ComboBox* m_alarmBox = nullptr;
    cutefish::ui::ComboBox* m_calendarBox = nullptr;
    cutefish::ui::Label* m_error = nullptr;
    cutefish::ui::View* m_repeatEndRow = nullptr;
    FormCard* m_repeatCard = nullptr;
    cutefish::ui::DialogButton* m_saveButton = nullptr;
};
