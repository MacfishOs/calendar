// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.h"
#include "calendarstore.h"
#include "eventfile.h"
#include "eventlayout.h"
#include "reminders.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

static QByteArray calendarText(const QByteArray &events)
{
    QByteArray text = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n" + events + "END:VCALENDAR\r\n";
    return text.replace("\n", "\r\n").replace("\r\r\n", "\r\n");
}

static QList<Occurrence> expand(const EventFile &file, QDate from, QDate to)
{
    QList<Occurrence> occurrences;
    file.expand(from, to, &occurrences);
    std::sort(occurrences.begin(), occurrences.end(), [](const Occurrence &a, const Occurrence &b) {
        return a.allDay ? a.firstDay < b.firstDay : a.start < b.start;
    });
    return occurrences;
}

static QDateTime zoned(const char *zone, QDate date, QTime time)
{
    return QDateTime(date, time, QTimeZone(zone)).toLocalTime();
}

static EventData timedEvent(const QString &title, const QDateTime &start, int minutes)
{
    EventData data;
    data.summary = title;
    data.start = start;
    data.end = start.addSecs(minutes * 60);
    return data;
}

static Occurrence occurrence(QDate first, QDate last, bool allDay, QTime start = {}, QTime end = {})
{
    Occurrence item;
    item.allDay = allDay;
    item.firstDay = first;
    item.lastDay = last;
    if (!allDay) {
        item.start = QDateTime(first, start);
        item.end = QDateTime(last, end);
    }
    return item;
}

class CoreTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void weeklyAcrossDaylightSaving();
    void untilInUtc();
    void exclusionsAndMovedInstance();
    void cancelledInstance();
    void allDayRepeat();
    void windowsZoneName();
    void createRoundTrip();
    void repeatPresets_data();
    void repeatPresets();
    void customRuleKept();
    void changeOneOccurrence();
    void moveSeriesShiftsChanges();
    void endBeforeOccurrence();
    void splitCountedSeries();
    void alarms();
    void importSplitsByUid();
    void layoutRows();
    void layoutDays();
    void storeRoundTrip();
    void storeSeesExternalChanges();
    void applicationScopesAndUndo();
    void applicationDragMove();
};

void CoreTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void CoreTest::weeklyAcrossDaylightSaving()
{
    const auto file = EventFile::fromData(calendarText(
            "BEGIN:VEVENT\nUID:weekly\nDTSTART;TZID=America/New_York:20260301T090000\n"
            "DTEND;TZID=America/New_York:20260301T100000\nRRULE:FREQ=WEEKLY;BYDAY=SU,WE;COUNT=6\n"
            "SUMMARY:Stand-up\\, weekly\nEND:VEVENT\n"));
    QVERIFY(file);
    QVERIFY(file->isRecurring());
    QCOMPARE(file->summary(), QStringLiteral("Stand-up, weekly"));

    const QList<Occurrence> occurrences = expand(*file, QDate(2026, 2, 1), QDate(2026, 5, 1));
    QCOMPARE(occurrences.size(), 6);
    // 9:00 in New York on both sides of the change on 8 March.
    QCOMPARE(occurrences.at(0).start, zoned("America/New_York", QDate(2026, 3, 1), QTime(9, 0)));
    QCOMPARE(occurrences.at(2).start, zoned("America/New_York", QDate(2026, 3, 8), QTime(9, 0)));
    QCOMPARE(occurrences.at(5).start, zoned("America/New_York", QDate(2026, 3, 18), QTime(9, 0)));
    QCOMPARE(occurrences.at(5).start.secsTo(occurrences.at(5).end), 3600);

    // A window in the middle of the series.
    const QList<Occurrence> middle = expand(*file, QDate(2026, 3, 9), QDate(2026, 3, 12));
    QCOMPARE(middle.size(), 1);
    QCOMPARE(middle.first().start, zoned("America/New_York", QDate(2026, 3, 11), QTime(9, 0)));
}

void CoreTest::untilInUtc()
{
    // 23:00 in Berlin is 21:00 UTC in summer: the instance on the UNTIL day is included.
    const auto file = EventFile::fromData(calendarText(
            "BEGIN:VEVENT\nUID:until\nDTSTART;TZID=Europe/Berlin:20260601T230000\nDURATION:PT30M\n"
            "RRULE:FREQ=DAILY;UNTIL=20260605T210000Z\nEND:VEVENT\n"));
    QVERIFY(file);
    QCOMPARE(expand(*file, QDate(2026, 5, 1), QDate(2026, 7, 1)).size(), 5);
    const EventData data = file->data();
    QCOMPARE(data.repeat.frequency, Repeat::Daily);
    QCOMPARE(data.repeat.until, zoned("Europe/Berlin", QDate(2026, 6, 5), QTime(23, 0)).date());
}

void CoreTest::exclusionsAndMovedInstance()
{
    const auto file = EventFile::fromData(calendarText(
            "BEGIN:VEVENT\nUID:moved\nDTSTART;TZID=Asia/Tokyo:20260105T100000\nDTEND;TZID=Asia/Tokyo:20260105T110000\n"
            "RRULE:FREQ=WEEKLY\nEXDATE;TZID=Asia/Tokyo:20260112T100000\nSUMMARY:Lesson\nEND:VEVENT\n"
            "BEGIN:VEVENT\nUID:moved\nRECURRENCE-ID;TZID=Asia/Tokyo:20260119T100000\n"
            "DTSTART;TZID=Asia/Tokyo:20260301T150000\nDTEND;TZID=Asia/Tokyo:20260301T160000\n"
            "SUMMARY:Lesson (moved)\nEND:VEVENT\n"));
    QVERIFY(file);

    const QList<Occurrence> january = expand(*file, QDate(2026, 1, 1), QDate(2026, 2, 1));
    QCOMPARE(january.size(), 2);
    QCOMPARE(january.at(0).start, zoned("Asia/Tokyo", QDate(2026, 1, 5), QTime(10, 0)));
    QCOMPARE(january.at(1).start, zoned("Asia/Tokyo", QDate(2026, 1, 26), QTime(10, 0)));

    // Moved in from January, it shows in March.
    const QList<Occurrence> march = expand(*file, QDate(2026, 3, 1), QDate(2026, 3, 2));
    QVERIFY(std::any_of(march.cbegin(), march.cend(), [](const Occurrence &item) {
        return item.summary == QLatin1String("Lesson (moved)");
    }));
}

void CoreTest::cancelledInstance()
{
    const auto file = EventFile::fromData(calendarText(
            "BEGIN:VEVENT\nUID:c\nDTSTART:20260105T100000Z\nDURATION:PT1H\nRRULE:FREQ=DAILY;COUNT=3\nEND:VEVENT\n"
            "BEGIN:VEVENT\nUID:c\nRECURRENCE-ID:20260106T100000Z\nDTSTART:20260106T100000Z\nDURATION:PT1H\n"
            "STATUS:CANCELLED\nEND:VEVENT\n"));
    QVERIFY(file);
    QCOMPARE(expand(*file, QDate(2026, 1, 1), QDate(2026, 2, 1)).size(), 2);
}

void CoreTest::allDayRepeat()
{
    const auto file = EventFile::fromData(calendarText(
            "BEGIN:VEVENT\nUID:birthday\nDTSTART;VALUE=DATE:20200229\nDTEND;VALUE=DATE:20200301\n"
            "RRULE:FREQ=YEARLY\nSUMMARY:Birthday\nEND:VEVENT\n"
            "BEGIN:VEVENT\nUID:trip\nDTSTART;VALUE=DATE:20261030\nDTEND;VALUE=DATE:20261103\nSUMMARY:Trip\nEND:VEVENT\n"));
    QVERIFY(file);
    QCOMPARE(file->data().repeat.frequency, Repeat::Yearly);
    // Only leap years have the day.
    QCOMPARE(expand(*file, QDate(2026, 1, 1), QDate(2029, 1, 1)).size(), 1);

    const auto trip = EventFile::split(calendarText(
            "BEGIN:VEVENT\nUID:trip\nDTSTART;VALUE=DATE:20261030\nDTEND;VALUE=DATE:20261103\nSUMMARY:Trip\nEND:VEVENT\n"));
    QCOMPARE(trip.size(), size_t(1));
    const QList<Occurrence> days = expand(*trip.front(), QDate(2026, 11, 1), QDate(2026, 11, 2));
    QCOMPARE(days.size(), 1);
    QCOMPARE(days.first().firstDay, QDate(2026, 10, 30));
    QCOMPARE(days.first().lastDay, QDate(2026, 11, 2));
    const EventData data = trip.front()->data();
    QVERIFY(data.allDay);
    QCOMPARE(data.endDate, QDate(2026, 11, 2));
}

void CoreTest::windowsZoneName()
{
    const auto file = EventFile::fromData(calendarText(
            "BEGIN:VEVENT\nUID:w\nDTSTART;TZID=\"Tokyo Standard Time\":20260105T100000\nDURATION:PT1H\nEND:VEVENT\n"));
    QVERIFY(file);
    const QList<Occurrence> occurrences = expand(*file, QDate(2026, 1, 1), QDate(2026, 2, 1));
    QCOMPARE(occurrences.size(), 1);
    QCOMPARE(occurrences.first().start, zoned("Asia/Tokyo", QDate(2026, 1, 5), QTime(10, 0)));
}

void CoreTest::createRoundTrip()
{
    EventData data = timedEvent(QStringLiteral("Dentist; at 9"), QDateTime(QDate(2026, 10, 12), QTime(9, 30)), 45);
    data.location = QStringLiteral("Main Street, 4");
    data.description = QStringLiteral("Two lines\nof notes");
    data.alarm = EventData::OffsetAlarm;
    data.alarmOffset = -15;
    const auto file = EventFile::create(data);
    const QByteArray text = file->toData();
    QVERIFY(text.contains("SEQUENCE:0"));
    if (!QTimeZone::systemTimeZoneId().isEmpty() && QTimeZone(QTimeZone::systemTimeZoneId()).isValid()) {
        QVERIFY2(text.contains("DTSTART;TZID=" + QTimeZone::systemTimeZoneId()), text.constData());
        QVERIFY(text.contains("BEGIN:VTIMEZONE"));
    }

    const auto again = EventFile::fromData(text);
    QVERIFY(again);
    QCOMPARE(again->uid(), file->uid());
    const EventData read = again->data();
    QCOMPARE(read.summary, data.summary);
    QCOMPARE(read.location, data.location);
    QCOMPARE(read.description, data.description);
    QCOMPARE(read.start, data.start);
    QCOMPARE(read.end, data.end);
    QCOMPARE(read.alarm, EventData::OffsetAlarm);
    QCOMPARE(read.alarmOffset, -15);
    QCOMPARE(read.repeat.frequency, Repeat::None);
}

void CoreTest::repeatPresets_data()
{
    QTest::addColumn<int>("frequency");
    QTest::addColumn<int>("count");
    QTest::addColumn<QDate>("until");
    QTest::addColumn<bool>("allDay");
    QTest::addColumn<int>("inFourWeeks");

    QTest::newRow("daily") << int(Repeat::Daily) << 0 << QDate() << false << 28;
    QTest::newRow("weekdays") << int(Repeat::Weekdays) << 0 << QDate() << false << 20;
    QTest::newRow("weekly") << int(Repeat::Weekly) << 0 << QDate() << false << 4;
    QTest::newRow("monthly") << int(Repeat::Monthly) << 0 << QDate() << false << 1;
    QTest::newRow("yearly") << int(Repeat::Yearly) << 0 << QDate() << true << 1;
    QTest::newRow("daily count") << int(Repeat::Daily) << 3 << QDate() << false << 3;
    QTest::newRow("daily until") << int(Repeat::Daily) << 0 << QDate(2026, 10, 9) << false << 5;
    QTest::newRow("all-day until") << int(Repeat::Daily) << 0 << QDate(2026, 10, 9) << true << 5;
}

void CoreTest::repeatPresets()
{
    QFETCH(int, frequency);
    QFETCH(int, count);
    QFETCH(QDate, until);
    QFETCH(bool, allDay);
    QFETCH(int, inFourWeeks);

    // Monday 5 October 2026.
    EventData data = timedEvent(QStringLiteral("Repeat"), QDateTime(QDate(2026, 10, 5), QTime(23, 30)), 60);
    data.allDay = allDay;
    data.startDate = data.endDate = QDate(2026, 10, 5);
    data.repeat.frequency = Repeat::Frequency(frequency);
    data.repeat.count = count;
    data.repeat.until = until;
    const auto file = EventFile::fromData(EventFile::create(data)->toData());
    QVERIFY(file);
    const EventData read = file->data();
    QCOMPARE(int(read.repeat.frequency), frequency);
    QCOMPARE(read.repeat.count, count);
    QCOMPARE(read.repeat.until, until);
    QCOMPARE(expand(*file, QDate(2026, 10, 5), QDate(2026, 11, 2)).size(), inFourWeeks);
}

void CoreTest::customRuleKept()
{
    const QByteArray text = calendarText(
            "BEGIN:VEVENT\nUID:custom\nDTSTART:20261005T090000Z\nDURATION:PT1H\n"
            "RRULE:FREQ=MONTHLY;BYDAY=2TU\nSUMMARY:Second Tuesday\nX-OTHER:kept\nEND:VEVENT\n");
    const auto file = EventFile::fromData(text);
    EventData data = file->data();
    QCOMPARE(data.repeat.frequency, Repeat::Custom);
    data.summary = QStringLiteral("Renamed");
    file->setData(data);
    const QByteArray written = file->toData();
    QVERIFY(written.contains("RRULE:FREQ=MONTHLY;BYDAY=2TU"));
    QVERIFY(written.contains("X-OTHER:kept"));
    QVERIFY(written.contains("SEQUENCE:1"));
}

void CoreTest::changeOneOccurrence()
{
    EventData data = timedEvent(QStringLiteral("Daily"), QDateTime(QDate(2026, 10, 5), QTime(9, 0)), 30);
    data.repeat.frequency = Repeat::Daily;
    const auto file = EventFile::create(data);

    QList<Occurrence> week = expand(*file, QDate(2026, 10, 5), QDate(2026, 10, 12));
    QCOMPARE(week.size(), 7);
    const QString third = week.at(2).recurrenceKey;
    QVERIFY(!third.isEmpty());
    QVERIFY(file->isFirstOccurrence(week.at(0).recurrenceKey));
    QVERIFY(!file->isFirstOccurrence(third));

    EventData one = file->data(third);
    QCOMPARE(one.start, QDateTime(QDate(2026, 10, 7), QTime(9, 0)));
    one.summary = QStringLiteral("Moved");
    one.start = one.start.addSecs(3600);
    one.end = one.end.addSecs(3600);
    file->setOccurrenceData(third, one);

    const auto again = EventFile::fromData(file->toData());
    week = expand(*again, QDate(2026, 10, 5), QDate(2026, 10, 12));
    QCOMPARE(week.size(), 7);
    QCOMPARE(week.at(2).summary, QStringLiteral("Moved"));
    QCOMPARE(week.at(2).start, QDateTime(QDate(2026, 10, 7), QTime(10, 0)));
    QCOMPARE(week.at(2).recurrenceKey, third);
    QCOMPARE(again->data(third).summary, QStringLiteral("Moved"));
    QCOMPARE(week.at(3).summary, QStringLiteral("Daily"));

    again->excludeOccurrence(third);
    week = expand(*again, QDate(2026, 10, 5), QDate(2026, 10, 12));
    QCOMPARE(week.size(), 6);
}

void CoreTest::moveSeriesShiftsChanges()
{
    EventData data = timedEvent(QStringLiteral("Weekly"), QDateTime(QDate(2026, 10, 5), QTime(9, 0)), 60);
    data.repeat.frequency = Repeat::Weekly;
    const auto file = EventFile::create(data);
    QList<Occurrence> month = expand(*file, QDate(2026, 10, 1), QDate(2026, 11, 1));
    QCOMPARE(month.size(), 4);
    file->excludeOccurrence(month.at(1).recurrenceKey);
    EventData renamed = file->data(month.at(2).recurrenceKey);
    renamed.summary = QStringLiteral("Special");
    file->setOccurrenceData(month.at(2).recurrenceKey, renamed);

    // Every occurrence a day later and an hour earlier.
    EventData moved = file->data();
    moved.start = QDateTime(QDate(2026, 10, 6), QTime(8, 0));
    moved.end = moved.start.addSecs(3600);
    file->setData(moved);

    month = expand(*file, QDate(2026, 10, 1), QDate(2026, 11, 1));
    QCOMPARE(month.size(), 3);
    QCOMPARE(month.at(0).start, QDateTime(QDate(2026, 10, 6), QTime(8, 0)));
    QCOMPARE(month.at(1).start, QDateTime(QDate(2026, 10, 20), QTime(8, 0)));
    QCOMPARE(month.at(1).summary, QStringLiteral("Special"));
    QCOMPARE(month.at(2).start, QDateTime(QDate(2026, 10, 27), QTime(8, 0)));

    // A new end keeps the same occurrences, changed and removed ones too.
    moved.repeat.until = QDate(2026, 12, 31);
    file->setData(moved);
    month = expand(*file, QDate(2026, 10, 1), QDate(2026, 11, 1));
    QCOMPARE(month.size(), 3);
    QCOMPARE(month.at(1).summary, QStringLiteral("Special"));
    QCOMPARE(expand(*file, QDate(2027, 1, 1), QDate(2027, 2, 1)).size(), 0);

    // Another rule makes other occurrences: changes to the old ones go.
    moved.repeat.frequency = Repeat::Daily;
    file->setData(moved);
    QCOMPARE(expand(*file, QDate(2026, 10, 6), QDate(2026, 10, 13)).size(), 7);
    QVERIFY(!file->toData().contains("RECURRENCE-ID"));
}

void CoreTest::endBeforeOccurrence()
{
    EventData data = timedEvent(QStringLiteral("Daily"), QDateTime(QDate(2026, 10, 5), QTime(9, 0)), 30);
    data.repeat.frequency = Repeat::Daily;
    data.repeat.count = 10;
    const auto file = EventFile::create(data);
    const QList<Occurrence> all = expand(*file, QDate(2026, 10, 1), QDate(2026, 11, 1));
    QCOMPARE(all.size(), 10);

    QVERIFY(!file->endBefore(all.first().recurrenceKey));
    QVERIFY(file->endBefore(all.at(4).recurrenceKey));
    const auto again = EventFile::fromData(file->toData());
    QCOMPARE(expand(*again, QDate(2026, 10, 1), QDate(2026, 11, 1)).size(), 4);
    QCOMPARE(again->data().repeat.until, QDate(2026, 10, 8));
}

void CoreTest::splitCountedSeries()
{
    EventData data = timedEvent(QStringLiteral("Course"), QDateTime(QDate(2026, 10, 5), QTime(18, 0)), 90);
    data.repeat.frequency = Repeat::Weekly;
    data.repeat.count = 8;
    const auto file = EventFile::create(data);
    const QList<Occurrence> all = expand(*file, QDate(2026, 10, 1), QDate(2027, 1, 1));
    QCOMPARE(all.size(), 8);

    EventData later = file->data(all.at(3).recurrenceKey);
    QVERIFY(!file->splitAt(all.first().recurrenceKey, later));
    later.summary = QStringLiteral("Course, new room");
    later.start = later.start.addSecs(1800);
    later.end = later.end.addSecs(1800);
    const auto rest = file->splitAt(all.at(3).recurrenceKey, later);
    QVERIFY(rest);
    QVERIFY(rest->uid() != file->uid());

    const QList<Occurrence> first = expand(*file, QDate(2026, 10, 1), QDate(2027, 1, 1));
    const QList<Occurrence> second = expand(*rest, QDate(2026, 10, 1), QDate(2027, 1, 1));
    QCOMPARE(first.size(), 3);
    QCOMPARE(second.size(), 5);
    QCOMPARE(second.first().start, QDateTime(QDate(2026, 10, 26), QTime(18, 30)));
    QCOMPARE(second.first().summary, QStringLiteral("Course, new room"));
    QCOMPARE(first.last().summary, QStringLiteral("Course"));
}

void CoreTest::alarms()
{
    EventData data = timedEvent(QStringLiteral("Call"), QDateTime(QDate(2026, 10, 5), QTime(9, 0)), 30);
    data.repeat.frequency = Repeat::Daily;
    data.alarm = EventData::OffsetAlarm;
    data.alarmOffset = -10;
    const auto file = EventFile::create(data);
    const auto due = file->alarms(QDateTime(QDate(2026, 10, 6), QTime(0, 0)), QDateTime(QDate(2026, 10, 8), QTime(0, 0)));
    QCOMPARE(due.size(), 2);
    QCOMPARE(due.at(0).first, QDateTime(QDate(2026, 10, 6), QTime(8, 50)));

    // An all-day event's alarm the evening before: 15 hours before midnight.
    EventData day;
    day.summary = QStringLiteral("Holiday");
    day.allDay = true;
    day.startDate = day.endDate = QDate(2026, 10, 10);
    day.alarm = EventData::OffsetAlarm;
    day.alarmOffset = -15 * 60;
    const auto allDay = EventFile::create(day);
    const auto evening = allDay->alarms(QDateTime(QDate(2026, 10, 9), QTime(0, 0)), QDateTime(QDate(2026, 10, 10), QTime(0, 0)));
    QCOMPARE(evening.size(), 1);
    QCOMPARE(evening.first().first, QDateTime(QDate(2026, 10, 9), QTime(9, 0)));
}

void CoreTest::importSplitsByUid()
{
    const auto files = EventFile::split(calendarText(
            "BEGIN:VTIMEZONE\nTZID:Custom Zone\nBEGIN:STANDARD\nDTSTART:19700101T000000\nTZOFFSETFROM:+0530\n"
            "TZOFFSETTO:+0530\nEND:STANDARD\nEND:VTIMEZONE\n"
            "BEGIN:VEVENT\nUID:a\nDTSTART;TZID=Custom Zone:20261005T100000\nDURATION:PT1H\nRRULE:FREQ=DAILY\nEND:VEVENT\n"
            "BEGIN:VEVENT\nUID:b\nDTSTART:20261006T100000Z\nDURATION:PT1H\nEND:VEVENT\n"
            "BEGIN:VEVENT\nUID:a\nRECURRENCE-ID;TZID=Custom Zone:20261006T100000\n"
            "DTSTART;TZID=Custom Zone:20261006T120000\nDURATION:PT1H\nEND:VEVENT\n"
            "BEGIN:VEVENT\nDTSTART;VALUE=DATE:20261007\nSUMMARY:No uid\nEND:VEVENT\n"));
    QCOMPARE(files.size(), size_t(3));
    QCOMPARE(files.at(0)->uid(), QStringLiteral("a"));
    const QList<Occurrence> a = expand(*files.at(0), QDate(2026, 10, 5), QDate(2026, 10, 7));
    QCOMPARE(a.size(), 2);
    // The file's own zone, +05:30, with no known name.
    QCOMPARE(a.at(0).start, QDateTime(QDate(2026, 10, 5), QTime(4, 30), QTimeZone::UTC).toLocalTime());
    QCOMPARE(a.at(1).start, QDateTime(QDate(2026, 10, 6), QTime(6, 30), QTimeZone::UTC).toLocalTime());
    QVERIFY(!files.at(2)->uid().isEmpty());
    QVERIFY(files.at(2)->data().allDay);
}

void CoreTest::layoutRows()
{
    const QDate monday(2026, 10, 5);
    QList<Occurrence> items;
    items << occurrence(monday.addDays(1), monday.addDays(3), true);                       // Tue-Thu bar
    items << occurrence(monday.addDays(1), monday.addDays(1), false, QTime(9, 0), QTime(10, 0));
    items << occurrence(monday.addDays(1), monday.addDays(1), false, QTime(11, 0), QTime(12, 0));
    items << occurrence(monday.addDays(1), monday.addDays(1), false, QTime(13, 0), QTime(14, 0));
    items << occurrence(monday.addDays(5), monday.addDays(8), true);                       // Sat to next Tue

    const EventLayout::Rows all = EventLayout::layoutRows(items, monday, 2, 0);
    QCOMPARE(all.lanes, 4);
    QVERIFY(all.more.isEmpty());

    const EventLayout::Rows limited = EventLayout::layoutRows(items, monday, 2, 3);
    // Tuesday shows the bar and one timed event, then "2 more".
    QCOMPARE(limited.more.size(), 1);
    QCOMPARE(limited.more.first().column, 1);
    QCOMPARE(limited.more.first().count, 2);

    int pieces = 0;
    for (const EventLayout::Bar &bar : limited.bars) {
        if (bar.index == 0) {
            QCOMPARE(bar.lane, 0);
            QCOMPARE(bar.column, 1);
            QCOMPARE(bar.span, 3);
        }
        if (bar.index == 4) {
            ++pieces;
            QCOMPARE(bar.continuesAfter, bar.row == 0);
            QCOMPARE(bar.continuesBefore, bar.row == 1);
        }
    }
    QCOMPARE(pieces, 2);

    const EventLayout::Rows strip = EventLayout::layoutRows(items, monday, 1, 0, true);
    QCOMPARE(strip.bars.size(), 2);
}

void CoreTest::layoutDays()
{
    const QDate day(2026, 10, 5);
    QList<Occurrence> items;
    items << occurrence(day, day, false, QTime(9, 0), QTime(11, 0));
    items << occurrence(day, day, false, QTime(9, 30), QTime(10, 0));
    items << occurrence(day, day, false, QTime(10, 30), QTime(12, 0));
    items << occurrence(day, day, false, QTime(14, 0), QTime(14, 0));
    items << occurrence(day, day.addDays(1), false, QTime(22, 0), QTime(2, 0));

    const QList<EventLayout::Block> blocks = EventLayout::layoutDays(items, day, 2, 30);
    QCOMPARE(blocks.size(), 6);
    const auto find = [&blocks](int index, int column) {
        for (const EventLayout::Block &block : blocks) {
            if (block.index == index && block.column == column)
                return block;
        }
        return EventLayout::Block();
    };
    QCOMPARE(find(0, 0).slotCount, 2);
    QCOMPARE(find(0, 0).slot, 0);
    QCOMPARE(find(1, 0).slot, 1);
    QCOMPARE(find(2, 0).slot, 1);
    QCOMPARE(find(3, 0).slotCount, 1);
    QCOMPARE(find(3, 0).endMinute, 14 * 60);
    QCOMPARE(find(4, 0).startMinute, 22 * 60);
    QCOMPARE(find(4, 0).endMinute, 24 * 60);
    QVERIFY(find(4, 0).continuesAfter);
    QCOMPARE(find(4, 1).startMinute, 0);
    QCOMPARE(find(4, 1).endMinute, 120);
    QVERIFY(find(4, 1).continuesBefore);
}

void CoreTest::storeRoundTrip()
{
    QTemporaryDir dir;
    CalendarStore store(dir.path());
    store.load();
    QVERIFY(store.isReady());
    QVERIFY(store.calendars().isEmpty());

    const QString work = store.createCalendar(QStringLiteral("Work"), QColor(0xff, 0x5c, 0x6d));
    const QString home = store.createCalendar(QStringLiteral("Home"), QColor(0x35, 0xbf, 0x56));
    QCOMPARE(store.calendars().first().name, QStringLiteral("Home"));
    QCOMPARE(QFile(dir.filePath(work + "/color")).exists(), true);

    const QString key = store.addEvent(work, EventFile::create(
            timedEvent(QStringLiteral("Review"), QDateTime(QDate(2026, 10, 7), QTime(14, 0)), 60)));
    QVERIFY(!key.isEmpty());
    QVERIFY(QFile::exists(dir.filePath(key)));
    QCOMPARE(store.occurrences(QDate(2026, 10, 1), QDate(2026, 11, 1)).size(), 1);
    QCOMPARE(store.occurrences(QDate(2026, 10, 1), QDate(2026, 11, 1), { work }).size(), 0);

    const QString moved = store.moveEvent(key, home);
    QVERIFY(moved.startsWith(home));
    QVERIFY(!QFile::exists(dir.filePath(key)));
    QCOMPARE(store.occurrences(QDate(2026, 10, 1), QDate(2026, 11, 1)).first().calendarId, home);

    // Read back from disk by a second store.
    CalendarStore other(dir.path());
    other.load();
    QCOMPARE(other.calendars().size(), 2);
    QCOMPARE(other.occurrences(QDate(2026, 10, 1), QDate(2026, 11, 1)).first().summary, QStringLiteral("Review"));

    const QByteArray exported = store.exportCalendar(home);
    QVERIFY(exported.contains("SUMMARY:Review"));
    QCOMPARE(store.importData(work, exported), 1);
    // A second import replaces the event with the same uid.
    QCOMPARE(store.importData(work, exported), 1);
    QCOMPARE(store.eventCount(work), 1);
    QCOMPARE(store.importData(work, "not a calendar"), -1);

    QVERIFY(store.removeEvent(moved));
    QCOMPARE(store.eventCount(home), 0);
}

void CoreTest::storeSeesExternalChanges()
{
    QTemporaryDir dir;
    CalendarStore store(dir.path());
    store.load();
    const QString id = store.createCalendar(QStringLiteral("Synced"), Qt::blue);
    QSignalSpy changed(&store, &CalendarStore::changed);

    QFile file(dir.filePath(id + "/external.ics"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(calendarText("BEGIN:VEVENT\nUID:ext\nDTSTART;VALUE=DATE:20261009\nSUMMARY:From sync\nEND:VEVENT\n"));
    file.close();

    QTRY_VERIFY(changed.count() > 0);
    QTRY_COMPARE(store.eventCount(id), 1);
}

void CoreTest::applicationScopesAndUndo()
{
    QTemporaryDir dir;
    CalendarStore store(dir.path());
    Application app(&store);
    store.load();
    QCOMPARE(app.calendars().size(), 1);
    const QString calendar = app.defaultCalendar();

    QVariantMap event = app.newEventData(QDateTime(QDate(2026, 10, 5), QTime(9, 0)),
                                         QDateTime(QDate(2026, 10, 5), QTime(10, 0)), false);
    event.insert(QStringLiteral("title"), QStringLiteral("Gym"));
    event.insert(QStringLiteral("repeat"), int(Repeat::Daily));
    QVERIFY(app.saveEvent(event, EventFile::AllEvents));
    QCOMPARE(store.eventCount(calendar), 1);

    QList<Occurrence> week = store.occurrences(QDate(2026, 10, 5), QDate(2026, 10, 12));
    QCOMPARE(week.size(), 7);
    std::sort(week.begin(), week.end(), [](const Occurrence &a, const Occurrence &b) { return a.start < b.start; });

    // Later ones get a new title: two events now.
    QVariantMap later = app.eventData(week.at(3).key, week.at(3).recurrenceKey);
    QCOMPARE(later.value("firstOccurrence").toBool(), false);
    later.insert(QStringLiteral("title"), QStringLiteral("Gym (new)"));
    QVERIFY(app.saveEvent(later, EventFile::FutureEvents));
    QCOMPARE(store.eventCount(calendar), 2);
    week = store.occurrences(QDate(2026, 10, 5), QDate(2026, 10, 12));
    QCOMPARE(week.size(), 7);
    QCOMPARE(std::count_if(week.cbegin(), week.cend(), [](const Occurrence &item) {
        return item.summary == QLatin1String("Gym (new)");
    }), 4);

    QVERIFY(app.canUndo());
    app.undo();
    QCOMPARE(store.eventCount(calendar), 1);
    week = store.occurrences(QDate(2026, 10, 5), QDate(2026, 10, 12));
    QCOMPARE(week.size(), 7);
    QVERIFY(std::all_of(week.cbegin(), week.cend(), [](const Occurrence &item) {
        return item.summary == QLatin1String("Gym");
    }));

    // Removing one occurrence, then the series, then undoing both.
    std::sort(week.begin(), week.end(), [](const Occurrence &a, const Occurrence &b) { return a.start < b.start; });
    QVERIFY(app.removeEvent(week.at(1).key, week.at(1).recurrenceKey, EventFile::ThisEvent));
    QCOMPARE(store.occurrences(QDate(2026, 10, 5), QDate(2026, 10, 12)).size(), 6);
    QVERIFY(app.removeEvent(week.at(0).key, week.at(0).recurrenceKey, EventFile::FutureEvents));
    QCOMPARE(store.eventCount(calendar), 0);
    app.undo();
    QCOMPARE(store.occurrences(QDate(2026, 10, 5), QDate(2026, 10, 12)).size(), 6);
    app.undo();
    QCOMPARE(store.occurrences(QDate(2026, 10, 5), QDate(2026, 10, 12)).size(), 7);
    app.undo();
    QCOMPARE(store.eventCount(calendar), 0);
    QVERIFY(!app.canUndo());

    // Hidden calendars drop out of the views and come back.
    QVERIFY(app.saveEvent(event, EventFile::AllEvents));
    QCOMPARE(app.busyDays(QDateTime(QDate(2026, 10, 5), QTime(0, 0)), 3).first().toBool(), true);
    app.setCalendarVisible(calendar, false);
    QCOMPARE(app.busyDays(QDateTime(QDate(2026, 10, 5), QTime(0, 0)), 3).first().toBool(), false);
    app.setCalendarVisible(calendar, true);
    QCOMPARE(app.search(QStringLiteral("gym")).size(), 1);
}

void CoreTest::applicationDragMove()
{
    QTemporaryDir dir;
    CalendarStore store(dir.path());
    Application app(&store);
    store.load();

    QVariantMap event = app.newEventData(QDateTime(QDate(2026, 10, 5), QTime(9, 0)),
                                         QDateTime(QDate(2026, 10, 5), QTime(10, 0)), false);
    event.insert(QStringLiteral("title"), QStringLiteral("Weekly"));
    event.insert(QStringLiteral("repeat"), int(Repeat::Weekly));
    event.insert(QStringLiteral("alarm"), int(EventData::OffsetAlarm));
    event.insert(QStringLiteral("alarmOffset"), -5);
    QVERIFY(app.saveEvent(event, EventFile::AllEvents));
    QList<Occurrence> items = store.occurrences(QDate(2026, 10, 12), QDate(2026, 10, 13));
    QCOMPARE(items.size(), 1);

    // The second occurrence alone, a day later and stretched by half an hour.
    QVERIFY(app.moveOccurrence(items.first().key, items.first().recurrenceKey, 1, 0, 1, 30, EventFile::ThisEvent));
    QCOMPARE(app.undoText(), QStringLiteral("Undo Move"));
    items = store.occurrences(QDate(2026, 10, 13), QDate(2026, 10, 14));
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().start, QDateTime(QDate(2026, 10, 13), QTime(9, 0)));
    QCOMPARE(items.first().end, QDateTime(QDate(2026, 10, 13), QTime(10, 30)));
    QCOMPARE(app.eventData(items.first().key, items.first().recurrenceKey).value("alarmOffset").toInt(), -5);

    // The whole series an hour later, from its third occurrence.
    items = store.occurrences(QDate(2026, 10, 19), QDate(2026, 10, 20));
    QVERIFY(app.moveOccurrence(items.first().key, items.first().recurrenceKey, 0, 60, 0, 60, EventFile::AllEvents));
    items = store.occurrences(QDate(2026, 10, 5), QDate(2026, 10, 6));
    QCOMPARE(items.first().start, QDateTime(QDate(2026, 10, 5), QTime(10, 0)));
    QCOMPARE(app.eventData(items.first().key, QString()).value("repeat").toInt(), int(Repeat::Weekly));
    const QList<Reminder> due = Reminders::due(store, QDateTime(QDate(2026, 10, 5), QTime(0, 0)),
                                               QDateTime(QDate(2026, 10, 6), QTime(0, 0)));
    QCOMPARE(due.size(), 1);
    QCOMPARE(due.first().time, QDateTime(QDate(2026, 10, 5), QTime(9, 55)));
}

QTEST_GUILESS_MAIN(CoreTest)

#include "tst_core.moc"
