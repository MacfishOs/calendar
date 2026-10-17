// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "calendarmodel.h"
#include "calendarstore.h"
#include "eventfile.h"
#include "eventlayout.h"
#include "reminders.h"

#include <base/event_loop.h>
#include <base/file.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;
namespace base = cutefish::base;

namespace {

// A directory removed with everything in it when the test ends.
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = (fs::temp_directory_path() / "calendar-test-XXXXXX").string();
        m_path = mkdtemp(pattern.data()) ? pattern : std::string();
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        fs::remove_all(m_path, error);
    }

    const std::string& path() const { return m_path; }
    std::string filePath(const std::string& name) const { return m_path + '/' + name; }

private:
    std::string m_path;
};

}

static std::string calendarText(const std::string& events)
{
    std::string text = "BEGIN:VCALENDAR\nVERSION:2.0\nPRODID:-//Test//EN\n" + events + "END:VCALENDAR\n";
    std::string crlf;
    for (char c : text) {
        if (c == '\n')
            crlf += "\r\n";
        else
            crlf.push_back(c);
    }
    return crlf;
}

static std::vector<Occurrence> expand(const EventFile& file, Date from, Date to)
{
    std::vector<Occurrence> occurrences;
    file.expand(from, to, &occurrences);
    std::sort(occurrences.begin(), occurrences.end(), [](const Occurrence& a, const Occurrence& b) {
        return a.allDay ? a.firstDay < b.firstDay : a.start < b.start;
    });
    return occurrences;
}

static DateTime zoned(const char* zone, Date date, Time time)
{
    return DateTime(date, time, TimeZone::fromId(zone)).toLocalTime();
}

static EventData timedEvent(const std::string& title, const DateTime& start, int minutes)
{
    EventData data;
    data.summary = title;
    data.start = start;
    data.end = start.addSecs(minutes * 60);
    return data;
}

static Occurrence occurrence(Date first, Date last, bool allDay, Time start = {}, Time end = {})
{
    Occurrence item;
    item.allDay = allDay;
    item.firstDay = first;
    item.lastDay = last;
    if (!allDay) {
        item.start = DateTime(first, start);
        item.end = DateTime(last, end);
    }
    return item;
}

// Runs the loop until the condition holds, at most for the timeout.
static bool runUntil(base::EventLoop& loop, const std::function<bool()>& condition,
                     std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    if (condition())
        return true;
    const int check = loop.startTimer(std::chrono::milliseconds(10), true, [&] {
        if (condition())
            loop.quit();
    });
    const int expire = loop.startTimer(timeout, false, [&] { loop.quit(); });
    loop.run();
    loop.stopTimer(check);
    loop.stopTimer(expire);
    return condition();
}

static bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

TEST(DateTimeTest, CalendarArithmetic)
{
    EXPECT_FALSE(Date().isValid());
    EXPECT_FALSE(Date(2026, 2, 29).isValid());
    EXPECT_TRUE(Date(2028, 2, 29).isValid());
    EXPECT_EQ(Date(2026, 10, 14).dayOfWeek(), 3);
    EXPECT_EQ(Date(2026, 1, 31).addMonths(1), Date(2026, 2, 28));
    EXPECT_EQ(Date(2024, 2, 29).addYears(1), Date(2025, 2, 28));
    EXPECT_EQ(Date(2026, 12, 31).addDays(1), Date(2027, 1, 1));
    EXPECT_EQ(Date(2026, 10, 1).daysTo(Date(2026, 11, 1)), 31);
    EXPECT_EQ(Date(2027, 1, 1).weekNumber(), 53);
    EXPECT_EQ(Date(2026, 1, 1).weekNumber(), 1);
    EXPECT_EQ(Date::fromIsoString("2026-10-05"), Date(2026, 10, 5));
    EXPECT_FALSE(Date::fromIsoString("2026-13-05").isValid());
    EXPECT_EQ(Date(2026, 10, 5).toCompactString(), "20261005");
}

TEST(DateTimeTest, ZonesAndTransitions)
{
    const TimeZone newYork = TimeZone::fromId("America/New_York");
    ASSERT_TRUE(newYork.isValid());
    EXPECT_FALSE(TimeZone::fromId("Nowhere/Special").isValid());

    // 9:00 in New York on both sides of the change on 8 March.
    const DateTime before(Date(2026, 3, 7), Time(9, 0), newYork);
    const DateTime after = before.addDays(1);
    EXPECT_EQ(after.time(), Time(9, 0));
    EXPECT_EQ(before.secsTo(after), 23 * 3600);

    // A time in the gap takes the offset from before it, so 2:30 is 3:30.
    const DateTime gap(Date(2026, 3, 8), Time(2, 30), newYork);
    EXPECT_EQ(gap.time(), Time(3, 30));
    // In the fold the first of the two times is taken.
    const DateTime fold(Date(2026, 11, 1), Time(1, 30), newYork);
    EXPECT_EQ(fold.toUtc().time(), Time(5, 30));

    const DateTime utc(Date(2026, 10, 5), Time(4, 30), TimeZone::utc());
    EXPECT_EQ(utc.toTimeZone(TimeZone::fixed(5 * 3600 + 1800)).time(), Time(10, 0));
    EXPECT_EQ(utc, utc.toLocalTime());
    EXPECT_LT(DateTime(), utc);
}

TEST(EventFileTest, WeeklyAcrossDaylightSaving)
{
    const auto file = EventFile::fromData(calendarText(
            "BEGIN:VEVENT\nUID:weekly\nDTSTART;TZID=America/New_York:20260301T090000\n"
            "DTEND;TZID=America/New_York:20260301T100000\nRRULE:FREQ=WEEKLY;BYDAY=SU,WE;COUNT=6\n"
            "SUMMARY:Stand-up\\, weekly\nEND:VEVENT\n"));
    ASSERT_TRUE(file);
    EXPECT_TRUE(file->isRecurring());
    EXPECT_EQ(file->summary(), "Stand-up, weekly");

    const std::vector<Occurrence> occurrences = expand(*file, Date(2026, 2, 1), Date(2026, 5, 1));
    ASSERT_EQ(occurrences.size(), 6u);
    // 9:00 in New York on both sides of the change on 8 March.
    EXPECT_EQ(occurrences[0].start, zoned("America/New_York", Date(2026, 3, 1), Time(9, 0)));
    EXPECT_EQ(occurrences[2].start, zoned("America/New_York", Date(2026, 3, 8), Time(9, 0)));
    EXPECT_EQ(occurrences[5].start, zoned("America/New_York", Date(2026, 3, 18), Time(9, 0)));
    EXPECT_EQ(occurrences[5].start.secsTo(occurrences[5].end), 3600);

    // A window in the middle of the series.
    const std::vector<Occurrence> middle = expand(*file, Date(2026, 3, 9), Date(2026, 3, 12));
    ASSERT_EQ(middle.size(), 1u);
    EXPECT_EQ(middle.front().start, zoned("America/New_York", Date(2026, 3, 11), Time(9, 0)));
}

TEST(EventFileTest, UntilInUtc)
{
    // 23:00 in Berlin is 21:00 UTC in summer: the instance on the UNTIL day is included.
    const auto file = EventFile::fromData(calendarText(
            "BEGIN:VEVENT\nUID:until\nDTSTART;TZID=Europe/Berlin:20260601T230000\nDURATION:PT30M\n"
            "RRULE:FREQ=DAILY;UNTIL=20260605T210000Z\nEND:VEVENT\n"));
    ASSERT_TRUE(file);
    EXPECT_EQ(expand(*file, Date(2026, 5, 1), Date(2026, 7, 1)).size(), 5u);
    const EventData data = file->data();
    EXPECT_EQ(data.repeat.frequency, Repeat::Daily);
    EXPECT_EQ(data.repeat.until, zoned("Europe/Berlin", Date(2026, 6, 5), Time(23, 0)).date());
}

TEST(EventFileTest, ExclusionsAndMovedInstance)
{
    const auto file = EventFile::fromData(calendarText(
            "BEGIN:VEVENT\nUID:moved\nDTSTART;TZID=Asia/Tokyo:20260105T100000\nDTEND;TZID=Asia/Tokyo:20260105T110000\n"
            "RRULE:FREQ=WEEKLY\nEXDATE;TZID=Asia/Tokyo:20260112T100000\nSUMMARY:Lesson\nEND:VEVENT\n"
            "BEGIN:VEVENT\nUID:moved\nRECURRENCE-ID;TZID=Asia/Tokyo:20260119T100000\n"
            "DTSTART;TZID=Asia/Tokyo:20260301T150000\nDTEND;TZID=Asia/Tokyo:20260301T160000\n"
            "SUMMARY:Lesson (moved)\nEND:VEVENT\n"));
    ASSERT_TRUE(file);

    const std::vector<Occurrence> january = expand(*file, Date(2026, 1, 1), Date(2026, 2, 1));
    ASSERT_EQ(january.size(), 2u);
    EXPECT_EQ(january[0].start, zoned("Asia/Tokyo", Date(2026, 1, 5), Time(10, 0)));
    EXPECT_EQ(january[1].start, zoned("Asia/Tokyo", Date(2026, 1, 26), Time(10, 0)));

    // Moved in from January, it shows in March.
    const std::vector<Occurrence> march = expand(*file, Date(2026, 3, 1), Date(2026, 3, 2));
    EXPECT_TRUE(std::any_of(march.cbegin(), march.cend(), [](const Occurrence& item) {
        return item.summary == "Lesson (moved)";
    }));
}

TEST(EventFileTest, CancelledInstance)
{
    const auto file = EventFile::fromData(calendarText(
            "BEGIN:VEVENT\nUID:c\nDTSTART:20260105T100000Z\nDURATION:PT1H\nRRULE:FREQ=DAILY;COUNT=3\nEND:VEVENT\n"
            "BEGIN:VEVENT\nUID:c\nRECURRENCE-ID:20260106T100000Z\nDTSTART:20260106T100000Z\nDURATION:PT1H\n"
            "STATUS:CANCELLED\nEND:VEVENT\n"));
    ASSERT_TRUE(file);
    EXPECT_EQ(expand(*file, Date(2026, 1, 1), Date(2026, 2, 1)).size(), 2u);
}

TEST(EventFileTest, AllDayRepeat)
{
    const auto file = EventFile::fromData(calendarText(
            "BEGIN:VEVENT\nUID:birthday\nDTSTART;VALUE=DATE:20200229\nDTEND;VALUE=DATE:20200301\n"
            "RRULE:FREQ=YEARLY\nSUMMARY:Birthday\nEND:VEVENT\n"
            "BEGIN:VEVENT\nUID:trip\nDTSTART;VALUE=DATE:20261030\nDTEND;VALUE=DATE:20261103\nSUMMARY:Trip\nEND:VEVENT\n"));
    ASSERT_TRUE(file);
    EXPECT_EQ(file->data().repeat.frequency, Repeat::Yearly);
    // Only leap years have the day.
    EXPECT_EQ(expand(*file, Date(2026, 1, 1), Date(2029, 1, 1)).size(), 1u);

    const auto trip = EventFile::split(calendarText(
            "BEGIN:VEVENT\nUID:trip\nDTSTART;VALUE=DATE:20261030\nDTEND;VALUE=DATE:20261103\nSUMMARY:Trip\nEND:VEVENT\n"));
    ASSERT_EQ(trip.size(), 1u);
    const std::vector<Occurrence> days = expand(*trip.front(), Date(2026, 11, 1), Date(2026, 11, 2));
    ASSERT_EQ(days.size(), 1u);
    EXPECT_EQ(days.front().firstDay, Date(2026, 10, 30));
    EXPECT_EQ(days.front().lastDay, Date(2026, 11, 2));
    const EventData data = trip.front()->data();
    EXPECT_TRUE(data.allDay);
    EXPECT_EQ(data.endDate, Date(2026, 11, 2));
}

TEST(EventFileTest, WindowsZoneName)
{
    const auto file = EventFile::fromData(calendarText(
            "BEGIN:VEVENT\nUID:w\nDTSTART;TZID=\"Tokyo Standard Time\":20260105T100000\nDURATION:PT1H\nEND:VEVENT\n"));
    ASSERT_TRUE(file);
    const std::vector<Occurrence> occurrences = expand(*file, Date(2026, 1, 1), Date(2026, 2, 1));
    ASSERT_EQ(occurrences.size(), 1u);
    EXPECT_EQ(occurrences.front().start, zoned("Asia/Tokyo", Date(2026, 1, 5), Time(10, 0)));
}

TEST(EventFileTest, CreateRoundTrip)
{
    EventData data = timedEvent("Dentist; at 9", DateTime(Date(2026, 10, 12), Time(9, 30)), 45);
    data.location = "Main Street, 4";
    data.description = "Two lines\nof notes";
    data.alarm = EventData::OffsetAlarm;
    data.alarmOffset = -15;
    const auto file = EventFile::create(data);
    const std::string text = file->toData();
    EXPECT_TRUE(contains(text, "SEQUENCE:0"));
    const std::string system = TimeZone().id();
    if (!system.empty() && TimeZone::fromId(system).isValid()) {
        EXPECT_TRUE(contains(text, "DTSTART;TZID=" + system)) << text;
        EXPECT_TRUE(contains(text, "BEGIN:VTIMEZONE"));
    }

    const auto again = EventFile::fromData(text);
    ASSERT_TRUE(again);
    EXPECT_EQ(again->uid(), file->uid());
    const EventData read = again->data();
    EXPECT_EQ(read.summary, data.summary);
    EXPECT_EQ(read.location, data.location);
    EXPECT_EQ(read.description, data.description);
    EXPECT_EQ(read.start, data.start);
    EXPECT_EQ(read.end, data.end);
    EXPECT_EQ(read.alarm, EventData::OffsetAlarm);
    EXPECT_EQ(read.alarmOffset, -15);
    EXPECT_EQ(read.repeat.frequency, Repeat::None);
}

struct RepeatPreset
{
    const char* name;
    Repeat::Frequency frequency;
    int count;
    Date until;
    bool allDay;
    size_t inFourWeeks;
};

class RepeatPresetTest : public testing::TestWithParam<RepeatPreset>
{
};

TEST_P(RepeatPresetTest, RoundTrip)
{
    const RepeatPreset& preset = GetParam();
    // Monday 5 October 2026.
    EventData data = timedEvent("Repeat", DateTime(Date(2026, 10, 5), Time(23, 30)), 60);
    data.allDay = preset.allDay;
    data.startDate = data.endDate = Date(2026, 10, 5);
    data.repeat.frequency = preset.frequency;
    data.repeat.count = preset.count;
    data.repeat.until = preset.until;
    const auto file = EventFile::fromData(EventFile::create(data)->toData());
    ASSERT_TRUE(file);
    const EventData read = file->data();
    EXPECT_EQ(read.repeat.frequency, preset.frequency);
    EXPECT_EQ(read.repeat.count, preset.count);
    EXPECT_EQ(read.repeat.until, preset.until);
    EXPECT_EQ(expand(*file, Date(2026, 10, 5), Date(2026, 11, 2)).size(), preset.inFourWeeks);
}

INSTANTIATE_TEST_SUITE_P(Presets, RepeatPresetTest, testing::Values(
        RepeatPreset{"daily", Repeat::Daily, 0, Date(), false, 28},
        RepeatPreset{"weekdays", Repeat::Weekdays, 0, Date(), false, 20},
        RepeatPreset{"weekly", Repeat::Weekly, 0, Date(), false, 4},
        RepeatPreset{"monthly", Repeat::Monthly, 0, Date(), false, 1},
        RepeatPreset{"yearly", Repeat::Yearly, 0, Date(), true, 1},
        RepeatPreset{"dailyCount", Repeat::Daily, 3, Date(), false, 3},
        RepeatPreset{"dailyUntil", Repeat::Daily, 0, Date(2026, 10, 9), false, 5},
        RepeatPreset{"allDayUntil", Repeat::Daily, 0, Date(2026, 10, 9), true, 5}),
    [](const testing::TestParamInfo<RepeatPreset>& info) { return std::string(info.param.name); });

TEST(EventFileTest, CustomRuleKept)
{
    const std::string text = calendarText(
            "BEGIN:VEVENT\nUID:custom\nDTSTART:20261005T090000Z\nDURATION:PT1H\n"
            "RRULE:FREQ=MONTHLY;BYDAY=2TU\nSUMMARY:Second Tuesday\nX-OTHER:kept\nEND:VEVENT\n");
    const auto file = EventFile::fromData(text);
    ASSERT_TRUE(file);
    EventData data = file->data();
    EXPECT_EQ(data.repeat.frequency, Repeat::Custom);
    data.summary = "Renamed";
    file->setData(data);
    const std::string written = file->toData();
    EXPECT_TRUE(contains(written, "RRULE:FREQ=MONTHLY;BYDAY=2TU"));
    EXPECT_TRUE(contains(written, "X-OTHER:kept"));
    EXPECT_TRUE(contains(written, "SEQUENCE:1"));
}

TEST(EventFileTest, ChangeOneOccurrence)
{
    EventData data = timedEvent("Daily", DateTime(Date(2026, 10, 5), Time(9, 0)), 30);
    data.repeat.frequency = Repeat::Daily;
    const auto file = EventFile::create(data);

    std::vector<Occurrence> week = expand(*file, Date(2026, 10, 5), Date(2026, 10, 12));
    ASSERT_EQ(week.size(), 7u);
    const std::string third = week[2].recurrenceKey;
    EXPECT_FALSE(third.empty());
    EXPECT_TRUE(file->isFirstOccurrence(week[0].recurrenceKey));
    EXPECT_FALSE(file->isFirstOccurrence(third));

    EventData one = file->data(third);
    EXPECT_EQ(one.start, DateTime(Date(2026, 10, 7), Time(9, 0)));
    one.summary = "Moved";
    one.start = one.start.addSecs(3600);
    one.end = one.end.addSecs(3600);
    file->setOccurrenceData(third, one);

    const auto again = EventFile::fromData(file->toData());
    week = expand(*again, Date(2026, 10, 5), Date(2026, 10, 12));
    ASSERT_EQ(week.size(), 7u);
    EXPECT_EQ(week[2].summary, "Moved");
    EXPECT_EQ(week[2].start, DateTime(Date(2026, 10, 7), Time(10, 0)));
    EXPECT_EQ(week[2].recurrenceKey, third);
    EXPECT_EQ(again->data(third).summary, "Moved");
    EXPECT_EQ(week[3].summary, "Daily");

    again->excludeOccurrence(third);
    week = expand(*again, Date(2026, 10, 5), Date(2026, 10, 12));
    EXPECT_EQ(week.size(), 6u);
}

TEST(EventFileTest, MoveSeriesShiftsChanges)
{
    EventData data = timedEvent("Weekly", DateTime(Date(2026, 10, 5), Time(9, 0)), 60);
    data.repeat.frequency = Repeat::Weekly;
    const auto file = EventFile::create(data);
    std::vector<Occurrence> month = expand(*file, Date(2026, 10, 1), Date(2026, 11, 1));
    ASSERT_EQ(month.size(), 4u);
    file->excludeOccurrence(month[1].recurrenceKey);
    EventData renamed = file->data(month[2].recurrenceKey);
    renamed.summary = "Special";
    file->setOccurrenceData(month[2].recurrenceKey, renamed);

    // Every occurrence a day later and an hour earlier.
    EventData moved = file->data();
    moved.start = DateTime(Date(2026, 10, 6), Time(8, 0));
    moved.end = moved.start.addSecs(3600);
    file->setData(moved);

    month = expand(*file, Date(2026, 10, 1), Date(2026, 11, 1));
    ASSERT_EQ(month.size(), 3u);
    EXPECT_EQ(month[0].start, DateTime(Date(2026, 10, 6), Time(8, 0)));
    EXPECT_EQ(month[1].start, DateTime(Date(2026, 10, 20), Time(8, 0)));
    EXPECT_EQ(month[1].summary, "Special");
    EXPECT_EQ(month[2].start, DateTime(Date(2026, 10, 27), Time(8, 0)));

    // A new end keeps the same occurrences, changed and removed ones too.
    moved.repeat.until = Date(2026, 12, 31);
    file->setData(moved);
    month = expand(*file, Date(2026, 10, 1), Date(2026, 11, 1));
    ASSERT_EQ(month.size(), 3u);
    EXPECT_EQ(month[1].summary, "Special");
    EXPECT_EQ(expand(*file, Date(2027, 1, 1), Date(2027, 2, 1)).size(), 0u);

    // Another rule makes other occurrences: changes to the old ones go.
    moved.repeat.frequency = Repeat::Daily;
    file->setData(moved);
    EXPECT_EQ(expand(*file, Date(2026, 10, 6), Date(2026, 10, 13)).size(), 7u);
    EXPECT_FALSE(contains(file->toData(), "RECURRENCE-ID"));
}

TEST(EventFileTest, EndBeforeOccurrence)
{
    EventData data = timedEvent("Daily", DateTime(Date(2026, 10, 5), Time(9, 0)), 30);
    data.repeat.frequency = Repeat::Daily;
    data.repeat.count = 10;
    const auto file = EventFile::create(data);
    const std::vector<Occurrence> all = expand(*file, Date(2026, 10, 1), Date(2026, 11, 1));
    ASSERT_EQ(all.size(), 10u);

    EXPECT_FALSE(file->endBefore(all.front().recurrenceKey));
    EXPECT_TRUE(file->endBefore(all[4].recurrenceKey));
    const auto again = EventFile::fromData(file->toData());
    EXPECT_EQ(expand(*again, Date(2026, 10, 1), Date(2026, 11, 1)).size(), 4u);
    EXPECT_EQ(again->data().repeat.until, Date(2026, 10, 8));
}

TEST(EventFileTest, SplitCountedSeries)
{
    EventData data = timedEvent("Course", DateTime(Date(2026, 10, 5), Time(18, 0)), 90);
    data.repeat.frequency = Repeat::Weekly;
    data.repeat.count = 8;
    const auto file = EventFile::create(data);
    const std::vector<Occurrence> all = expand(*file, Date(2026, 10, 1), Date(2027, 1, 1));
    ASSERT_EQ(all.size(), 8u);

    EventData later = file->data(all[3].recurrenceKey);
    EXPECT_FALSE(file->splitAt(all.front().recurrenceKey, later));
    later.summary = "Course, new room";
    later.start = later.start.addSecs(1800);
    later.end = later.end.addSecs(1800);
    const auto rest = file->splitAt(all[3].recurrenceKey, later);
    ASSERT_TRUE(rest);
    EXPECT_NE(rest->uid(), file->uid());

    const std::vector<Occurrence> first = expand(*file, Date(2026, 10, 1), Date(2027, 1, 1));
    const std::vector<Occurrence> second = expand(*rest, Date(2026, 10, 1), Date(2027, 1, 1));
    ASSERT_EQ(first.size(), 3u);
    ASSERT_EQ(second.size(), 5u);
    EXPECT_EQ(second.front().start, DateTime(Date(2026, 10, 26), Time(18, 30)));
    EXPECT_EQ(second.front().summary, "Course, new room");
    EXPECT_EQ(first.back().summary, "Course");
}

TEST(EventFileTest, Alarms)
{
    EventData data = timedEvent("Call", DateTime(Date(2026, 10, 5), Time(9, 0)), 30);
    data.repeat.frequency = Repeat::Daily;
    data.alarm = EventData::OffsetAlarm;
    data.alarmOffset = -10;
    const auto file = EventFile::create(data);
    const auto due = file->alarms(DateTime(Date(2026, 10, 6), Time(0, 0)), DateTime(Date(2026, 10, 8), Time(0, 0)));
    ASSERT_EQ(due.size(), 2u);
    std::vector<DateTime> times;
    for (const auto& [time, item] : due)
        times.push_back(time);
    std::sort(times.begin(), times.end());
    EXPECT_EQ(times.front(), DateTime(Date(2026, 10, 6), Time(8, 50)));

    // An all-day event's alarm the evening before: 15 hours before midnight.
    EventData day;
    day.summary = "Holiday";
    day.allDay = true;
    day.startDate = day.endDate = Date(2026, 10, 10);
    day.alarm = EventData::OffsetAlarm;
    day.alarmOffset = -15 * 60;
    const auto allDay = EventFile::create(day);
    const auto evening = allDay->alarms(DateTime(Date(2026, 10, 9), Time(0, 0)), DateTime(Date(2026, 10, 10), Time(0, 0)));
    ASSERT_EQ(evening.size(), 1u);
    EXPECT_EQ(evening.front().first, DateTime(Date(2026, 10, 9), Time(9, 0)));
}

// An alarm at a set time goes off then, days before the event too, and only
// once for a repeating event.
TEST(EventFileTest, AlarmsAtASetTime)
{
    const auto single = EventFile::fromData(calendarText(
        "BEGIN:VEVENT\nUID:fixed-single\nDTSTART:20261020T100000Z\nDTEND:20261020T110000Z\nSUMMARY:Exam\n"
        "BEGIN:VALARM\nACTION:DISPLAY\nTRIGGER;VALUE=DATE-TIME:20261015T090000Z\nEND:VALARM\nEND:VEVENT\n"));
    ASSERT_TRUE(single);
    const DateTime trigger = DateTime(Date(2026, 10, 15), Time(9, 0), TimeZone::utc());
    const auto due = single->alarms(trigger.addSecs(-3600), trigger.addSecs(3600));
    ASSERT_EQ(due.size(), 1u);
    EXPECT_EQ(due.front().first, trigger);
    EXPECT_EQ(due.front().second.start, DateTime(Date(2026, 10, 20), Time(10, 0), TimeZone::utc()));

    const auto daily = EventFile::fromData(calendarText(
        "BEGIN:VEVENT\nUID:fixed-daily\nDTSTART:20261010T100000Z\nDTEND:20261010T110000Z\nRRULE:FREQ=DAILY\n"
        "SUMMARY:Standup\nBEGIN:VALARM\nACTION:DISPLAY\nTRIGGER;VALUE=DATE-TIME:20261015T090000Z\nEND:VALARM\n"
        "END:VEVENT\n"));
    ASSERT_TRUE(daily);
    const auto once = daily->alarms(trigger.addDays(-2), trigger.addDays(2));
    ASSERT_EQ(once.size(), 1u);
    EXPECT_EQ(once.front().first, trigger);
    EXPECT_EQ(once.front().second.start, DateTime(Date(2026, 10, 15), Time(10, 0), TimeZone::utc()));
}

TEST(EventFileTest, ImportSplitsByUid)
{
    const auto files = EventFile::split(calendarText(
            "BEGIN:VTIMEZONE\nTZID:Custom Zone\nBEGIN:STANDARD\nDTSTART:19700101T000000\nTZOFFSETFROM:+0530\n"
            "TZOFFSETTO:+0530\nEND:STANDARD\nEND:VTIMEZONE\n"
            "BEGIN:VEVENT\nUID:a\nDTSTART;TZID=Custom Zone:20261005T100000\nDURATION:PT1H\nRRULE:FREQ=DAILY\nEND:VEVENT\n"
            "BEGIN:VEVENT\nUID:b\nDTSTART:20261006T100000Z\nDURATION:PT1H\nEND:VEVENT\n"
            "BEGIN:VEVENT\nUID:a\nRECURRENCE-ID;TZID=Custom Zone:20261006T100000\n"
            "DTSTART;TZID=Custom Zone:20261006T120000\nDURATION:PT1H\nEND:VEVENT\n"
            "BEGIN:VEVENT\nDTSTART;VALUE=DATE:20261007\nSUMMARY:No uid\nEND:VEVENT\n"));
    ASSERT_EQ(files.size(), 3u);
    EXPECT_EQ(files[0]->uid(), "a");
    const std::vector<Occurrence> a = expand(*files[0], Date(2026, 10, 5), Date(2026, 10, 7));
    ASSERT_EQ(a.size(), 2u);
    // The file's own zone, +05:30, with no known name.
    EXPECT_EQ(a[0].start, DateTime(Date(2026, 10, 5), Time(4, 30), TimeZone::utc()));
    EXPECT_EQ(a[1].start, DateTime(Date(2026, 10, 6), Time(6, 30), TimeZone::utc()));
    EXPECT_FALSE(files[2]->uid().empty());
    EXPECT_TRUE(files[2]->data().allDay);
}

TEST(EventLayoutTest, Rows)
{
    const Date monday(2026, 10, 5);
    std::vector<Occurrence> items;
    items.push_back(occurrence(monday.addDays(1), monday.addDays(3), true)); // Tue-Thu bar
    items.push_back(occurrence(monday.addDays(1), monday.addDays(1), false, Time(9, 0), Time(10, 0)));
    items.push_back(occurrence(monday.addDays(1), monday.addDays(1), false, Time(11, 0), Time(12, 0)));
    items.push_back(occurrence(monday.addDays(1), monday.addDays(1), false, Time(13, 0), Time(14, 0)));
    items.push_back(occurrence(monday.addDays(5), monday.addDays(8), true)); // Sat to next Tue

    const EventLayout::Rows all = EventLayout::layoutRows(items, monday, 2, 0);
    EXPECT_EQ(all.lanes, 4);
    EXPECT_TRUE(all.more.empty());

    const EventLayout::Rows limited = EventLayout::layoutRows(items, monday, 2, 3);
    // Tuesday shows the bar and one timed event, then "2 more".
    ASSERT_EQ(limited.more.size(), 1u);
    EXPECT_EQ(limited.more.front().column, 1);
    EXPECT_EQ(limited.more.front().count, 2);

    int pieces = 0;
    for (const EventLayout::Bar& bar : limited.bars) {
        if (bar.index == 0) {
            EXPECT_EQ(bar.lane, 0);
            EXPECT_EQ(bar.column, 1);
            EXPECT_EQ(bar.span, 3);
        }
        if (bar.index == 4) {
            ++pieces;
            EXPECT_EQ(bar.continuesAfter, bar.row == 0);
            EXPECT_EQ(bar.continuesBefore, bar.row == 1);
        }
    }
    EXPECT_EQ(pieces, 2);

    const EventLayout::Rows strip = EventLayout::layoutRows(items, monday, 1, 0, true);
    EXPECT_EQ(strip.bars.size(), 2u);
}

TEST(EventLayoutTest, Days)
{
    const Date day(2026, 10, 5);
    std::vector<Occurrence> items;
    items.push_back(occurrence(day, day, false, Time(9, 0), Time(11, 0)));
    items.push_back(occurrence(day, day, false, Time(9, 30), Time(10, 0)));
    items.push_back(occurrence(day, day, false, Time(10, 30), Time(12, 0)));
    items.push_back(occurrence(day, day, false, Time(14, 0), Time(14, 0)));
    items.push_back(occurrence(day, day.addDays(1), false, Time(22, 0), Time(2, 0)));

    const std::vector<EventLayout::Block> blocks = EventLayout::layoutDays(items, day, 2, 30);
    ASSERT_EQ(blocks.size(), 6u);
    const auto find = [&blocks](int index, int column) {
        for (const EventLayout::Block& block : blocks) {
            if (block.index == index && block.column == column)
                return block;
        }
        return EventLayout::Block();
    };
    EXPECT_EQ(find(0, 0).slotCount, 2);
    EXPECT_EQ(find(0, 0).slot, 0);
    EXPECT_EQ(find(1, 0).slot, 1);
    EXPECT_EQ(find(2, 0).slot, 1);
    EXPECT_EQ(find(3, 0).slotCount, 1);
    EXPECT_EQ(find(3, 0).endMinute, 14 * 60);
    EXPECT_EQ(find(4, 0).startMinute, 22 * 60);
    EXPECT_EQ(find(4, 0).endMinute, 24 * 60);
    EXPECT_TRUE(find(4, 0).continuesAfter);
    EXPECT_EQ(find(4, 1).startMinute, 0);
    EXPECT_EQ(find(4, 1).endMinute, 120);
    EXPECT_TRUE(find(4, 1).continuesBefore);
}

// A file the store cannot read keeps its name: a new event of the same UID
// is written next to it, not over it.
TEST(StoreTest, UnreadableFileIsNotOverwritten)
{
    base::EventLoop loop;
    TemporaryDirectory dir;
    CalendarStore store(dir.path());
    store.load();
    const std::string work = store.createCalendar("Work", Rgb{0xff, 0x5c, 0x6d});
    ASSERT_TRUE(base::file::writeAtomically(dir.filePath(work + "/same-uid.ics"), "BEGIN:VCALENDAR\r\nbroken"));
    store.load();
    EXPECT_EQ(store.eventCount(work), 0);

    const std::string key = store.addEvent(work, EventFile::fromData(calendarText(
        "BEGIN:VEVENT\nUID:same-uid\nDTSTART:20261020T100000Z\nDTEND:20261020T110000Z\nSUMMARY:New\nEND:VEVENT\n")));
    ASSERT_FALSE(key.empty());
    EXPECT_EQ(key, work + "/same-uid-2.ics");
    EXPECT_EQ(base::file::read(dir.filePath(work + "/same-uid.ics")).value_or(""), "BEGIN:VCALENDAR\r\nbroken");
}

TEST(StoreTest, RoundTrip)
{
    base::EventLoop loop;
    TemporaryDirectory dir;
    CalendarStore store(dir.path());
    store.load();
    EXPECT_TRUE(store.isReady());
    EXPECT_TRUE(store.calendars().empty());

    const std::string work = store.createCalendar("Work", Rgb{0xff, 0x5c, 0x6d});
    const std::string home = store.createCalendar("Home", Rgb{0x35, 0xbf, 0x56});
    EXPECT_EQ(store.calendars().front().name, "Home");
    EXPECT_TRUE(fs::exists(dir.filePath(work + "/color")));
    EXPECT_EQ(base::file::read(dir.filePath(work + "/color")).value_or(""), "#FF5C6D\n");

    const std::string key = store.addEvent(work, EventFile::create(
            timedEvent("Review", DateTime(Date(2026, 10, 7), Time(14, 0)), 60)));
    ASSERT_FALSE(key.empty());
    EXPECT_TRUE(fs::exists(dir.filePath(key)));
    EXPECT_EQ(store.occurrences(Date(2026, 10, 1), Date(2026, 11, 1)).size(), 1u);
    EXPECT_EQ(store.occurrences(Date(2026, 10, 1), Date(2026, 11, 1), {work}).size(), 0u);

    const std::string moved = store.moveEvent(key, home);
    EXPECT_TRUE(moved.starts_with(home));
    EXPECT_FALSE(fs::exists(dir.filePath(key)));
    EXPECT_EQ(store.occurrences(Date(2026, 10, 1), Date(2026, 11, 1)).front().calendarId, home);

    // Read back from disk by a second store.
    CalendarStore other(dir.path());
    other.load();
    EXPECT_EQ(other.calendars().size(), 2u);
    EXPECT_EQ(other.occurrences(Date(2026, 10, 1), Date(2026, 11, 1)).front().summary, "Review");

    const std::string exported = store.exportCalendar(home);
    EXPECT_TRUE(contains(exported, "SUMMARY:Review"));
    EXPECT_EQ(store.importData(work, exported), 1);
    // A second import replaces the event with the same uid.
    EXPECT_EQ(store.importData(work, exported), 1);
    EXPECT_EQ(store.eventCount(work), 1);
    EXPECT_EQ(store.importData(work, "not a calendar"), -1);

    EXPECT_TRUE(store.removeEvent(moved));
    EXPECT_EQ(store.eventCount(home), 0);
}

TEST(StoreTest, SeesExternalChanges)
{
    base::EventLoop loop;
    TemporaryDirectory dir;
    CalendarStore store(dir.path());
    store.load();
    const std::string id = store.createCalendar("Synced", Rgb{0, 0, 0xff});
    int changes = 0;
    const base::Subscription subscription = store.changed.connect([&changes] { ++changes; });

    ASSERT_TRUE(base::file::writeAtomically(dir.filePath(id + "/external.ics"), calendarText(
            "BEGIN:VEVENT\nUID:ext\nDTSTART;VALUE=DATE:20261009\nSUMMARY:From sync\nEND:VEVENT\n")));

    EXPECT_TRUE(runUntil(loop, [&] { return changes > 0 && store.eventCount(id) == 1; }));
}

TEST(StoreTest, LoadsAsynchronously)
{
    base::EventLoop loop;
    TemporaryDirectory dir;
    {
        CalendarStore store(dir.path());
        store.load();
        const std::string id = store.createCalendar("Work", Rgb{0xff, 0x5c, 0x6d});
        store.addEvent(id, EventFile::create(timedEvent("Review", DateTime(Date(2026, 10, 7), Time(14, 0)), 60)));
    }
    CalendarStore store(dir.path());
    store.loadAsync();
    EXPECT_FALSE(store.isReady());
    EXPECT_TRUE(runUntil(loop, [&] { return store.isReady(); }));
    EXPECT_EQ(store.occurrences(Date(2026, 10, 1), Date(2026, 11, 1)).size(), 1u);
}

TEST(ModelTest, ScopesAndUndo)
{
    base::EventLoop loop;
    TemporaryDirectory dir;
    CalendarStore store(dir.filePath("calendars"));
    CalendarModel app(&store, dir.filePath("calendar.conf"));
    store.load();
    EXPECT_EQ(app.calendars().size(), 1u);
    const std::string calendar = app.defaultCalendar();

    EventForm event = app.newEventData(DateTime(Date(2026, 10, 5), Time(9, 0)), DateTime(Date(2026, 10, 5), Time(10, 0)),
                                       false);
    event.title = "Gym";
    event.repeat = Repeat::Daily;
    EXPECT_TRUE(app.saveEvent(event, EventFile::AllEvents));
    EXPECT_EQ(store.eventCount(calendar), 1);

    std::vector<Occurrence> week = store.occurrences(Date(2026, 10, 5), Date(2026, 10, 12));
    ASSERT_EQ(week.size(), 7u);
    const auto byStart = [](const Occurrence& a, const Occurrence& b) { return a.start < b.start; };
    std::sort(week.begin(), week.end(), byStart);

    // Later ones get a new title: two events now.
    std::optional<EventForm> later = app.eventData(week[3].key, week[3].recurrenceKey);
    ASSERT_TRUE(later);
    EXPECT_FALSE(later->firstOccurrence);
    later->title = "Gym (new)";
    EXPECT_TRUE(app.saveEvent(*later, EventFile::FutureEvents));
    EXPECT_EQ(store.eventCount(calendar), 2);
    week = store.occurrences(Date(2026, 10, 5), Date(2026, 10, 12));
    EXPECT_EQ(week.size(), 7u);
    EXPECT_EQ(std::count_if(week.cbegin(), week.cend(), [](const Occurrence& item) {
        return item.summary == "Gym (new)";
    }), 4);

    EXPECT_TRUE(app.canUndo());
    app.undo();
    EXPECT_EQ(store.eventCount(calendar), 1);
    week = store.occurrences(Date(2026, 10, 5), Date(2026, 10, 12));
    EXPECT_EQ(week.size(), 7u);
    EXPECT_TRUE(std::all_of(week.cbegin(), week.cend(), [](const Occurrence& item) { return item.summary == "Gym"; }));

    // Removing one occurrence, then the series, then undoing both.
    std::sort(week.begin(), week.end(), byStart);
    EXPECT_TRUE(app.removeEvent(week[1].key, week[1].recurrenceKey, EventFile::ThisEvent));
    EXPECT_EQ(store.occurrences(Date(2026, 10, 5), Date(2026, 10, 12)).size(), 6u);
    EXPECT_TRUE(app.removeEvent(week[0].key, week[0].recurrenceKey, EventFile::FutureEvents));
    EXPECT_EQ(store.eventCount(calendar), 0);
    app.undo();
    EXPECT_EQ(store.occurrences(Date(2026, 10, 5), Date(2026, 10, 12)).size(), 6u);
    app.undo();
    EXPECT_EQ(store.occurrences(Date(2026, 10, 5), Date(2026, 10, 12)).size(), 7u);
    app.undo();
    EXPECT_EQ(store.eventCount(calendar), 0);
    EXPECT_FALSE(app.canUndo());

    // Hidden calendars drop out of the views and come back.
    EXPECT_TRUE(app.saveEvent(event, EventFile::AllEvents));
    EXPECT_TRUE(app.busyDays(Date(2026, 10, 5), 3).front());
    app.setCalendarVisible(calendar, false);
    EXPECT_FALSE(app.busyDays(Date(2026, 10, 5), 3).front());
    app.setCalendarVisible(calendar, true);
    EXPECT_EQ(app.search("gym").size(), 1u);
}

TEST(ModelTest, DragMove)
{
    base::EventLoop loop;
    TemporaryDirectory dir;
    CalendarStore store(dir.filePath("calendars"));
    CalendarModel app(&store, dir.filePath("calendar.conf"));
    store.load();

    EventForm event = app.newEventData(DateTime(Date(2026, 10, 5), Time(9, 0)), DateTime(Date(2026, 10, 5), Time(10, 0)),
                                       false);
    event.title = "Weekly";
    event.repeat = Repeat::Weekly;
    event.alarm = EventData::OffsetAlarm;
    event.alarmOffset = -5;
    EXPECT_TRUE(app.saveEvent(event, EventFile::AllEvents));
    std::vector<Occurrence> items = store.occurrences(Date(2026, 10, 12), Date(2026, 10, 13));
    ASSERT_EQ(items.size(), 1u);

    // The second occurrence alone, a day later and stretched by half an hour.
    EXPECT_TRUE(app.moveOccurrence(items.front().key, items.front().recurrenceKey, 1, 0, 1, 30, EventFile::ThisEvent));
    EXPECT_EQ(app.undoText(), "Undo Move");
    items = store.occurrences(Date(2026, 10, 13), Date(2026, 10, 14));
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items.front().start, DateTime(Date(2026, 10, 13), Time(9, 0)));
    EXPECT_EQ(items.front().end, DateTime(Date(2026, 10, 13), Time(10, 30)));
    EXPECT_EQ(app.eventData(items.front().key, items.front().recurrenceKey)->alarmOffset, -5);

    // The whole series an hour later, from its third occurrence.
    items = store.occurrences(Date(2026, 10, 19), Date(2026, 10, 20));
    ASSERT_FALSE(items.empty());
    EXPECT_TRUE(app.moveOccurrence(items.front().key, items.front().recurrenceKey, 0, 60, 0, 60, EventFile::AllEvents));
    items = store.occurrences(Date(2026, 10, 5), Date(2026, 10, 6));
    ASSERT_FALSE(items.empty());
    EXPECT_EQ(items.front().start, DateTime(Date(2026, 10, 5), Time(10, 0)));
    EXPECT_EQ(app.eventData(items.front().key, {})->repeat, int(Repeat::Weekly));
    const std::vector<Reminder> due = Reminders::due(store, DateTime(Date(2026, 10, 5), Time(0, 0)),
                                                     DateTime(Date(2026, 10, 6), Time(0, 0)));
    ASSERT_EQ(due.size(), 1u);
    EXPECT_EQ(due.front().time, DateTime(Date(2026, 10, 5), Time(9, 55)));
}

TEST(ModelTest, SearchFindsAPastSeriesWithoutItsFirstOccurrence)
{
    base::EventLoop loop;
    TemporaryDirectory dir;
    CalendarStore store(dir.filePath("calendars"));
    CalendarModel app(&store, dir.filePath("calendar.conf"));
    store.load();

    EventForm event = app.newEventData(DateTime(Date(2020, 1, 6), Time(9, 0)), DateTime(Date(2020, 1, 6), Time(10, 0)),
                                       false);
    event.title = "Standup";
    event.repeat = Repeat::Daily;
    event.repeatEnd = 2;
    event.repeatCount = 3;
    EXPECT_TRUE(app.saveEvent(event, EventFile::AllEvents));
    const std::vector<Occurrence> first = store.occurrences(Date(2020, 1, 6), Date(2020, 1, 7));
    ASSERT_EQ(first.size(), 1u);
    EXPECT_TRUE(app.removeEvent(first.front().key, first.front().recurrenceKey, EventFile::ThisEvent));

    const std::vector<EventItem> found = app.search("standup");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_FALSE(found.front().upcoming);
    EXPECT_EQ(found.front().firstDay, Date(2020, 1, 7));
}

TEST(ModelTest, SettingsKeepTheirFormat)
{
    base::EventLoop loop;
    TemporaryDirectory dir;
    const std::string path = dir.filePath("calendar.conf");
    ASSERT_TRUE(base::file::writeAtomically(path, "[General]\nview=week\nsidebarVisible=false\n"
                                                  "\n[WindowSize]\nMain=@Size(931 625)\n"));
    CalendarStore store(dir.filePath("calendars"));
    {
        CalendarModel app(&store, path);
        EXPECT_EQ(app.view(), "week");
        EXPECT_FALSE(app.sidebarVisible());
        app.setView("year");
        app.setShowWeekNumbers(true);
        app.setCalendarVisible("work", false);
    }
    const std::string written = base::file::read(path).value_or("");
    EXPECT_TRUE(contains(written, "view=year")) << written;
    EXPECT_TRUE(contains(written, "showWeekNumbers=true")) << written;
    EXPECT_TRUE(contains(written, "[Calendars]\nhidden=work")) << written;
    EXPECT_TRUE(contains(written, "Main=@Size(931 625)")) << written;
}
