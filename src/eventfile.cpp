// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eventfile.h"
#include "timezones.h"

#include <QHash>
#include <QSet>
#include <QUuid>

#include <libical/ical.h>

#include <algorithm>
#include <functional>

namespace {

struct Zone
{
    enum Kind { Floating, Utc, Named, Custom };

    Kind kind = Floating;
    QTimeZone zone;
    // As the file writes it, for Named and Custom.
    QByteArray tzid;
    // A VTIMEZONE of the file that no known zone matches.
    icaltimezone *custom = nullptr;
};

struct Time
{
    icaltimetype wall = icaltime_null_time();
    Zone zone;

    bool isNull() const { return icaltime_is_null_time(wall); }
    bool isDate() const { return wall.is_date; }
};

// When an event happens: its first start and how long each occurrence lasts.
struct Series
{
    Time start;
    bool allDay = false;
    qint64 seconds = 0;
    int days = 1;
};

}

// Repeating events are expanded at most this far.
static const int MaximumInstances = 100000;

// libical walks children with one iterator per parent, which any lookup on
// the parent moves: loops take a copy of the children first.
static QList<icalcomponent *> components(icalcomponent *parent, icalcomponent_kind kind)
{
    QList<icalcomponent *> list;
    for (icalcomponent *child = icalcomponent_get_first_component(parent, kind); child;
         child = icalcomponent_get_next_component(parent, kind))
        list.append(child);
    return list;
}

static QList<icalproperty *> properties(icalcomponent *parent, icalproperty_kind kind)
{
    QList<icalproperty *> list;
    for (icalproperty *property = icalcomponent_get_first_property(parent, kind); property;
         property = icalcomponent_get_next_property(parent, kind))
        list.append(property);
    return list;
}

static Zone utcZone()
{
    Zone zone;
    zone.kind = Zone::Utc;
    return zone;
}

static Zone systemZone()
{
    Zone zone;
    const QTimeZone system = TimeZones::system();
    if (system.timeSpec() == Qt::TimeZone) {
        zone.kind = Zone::Named;
        zone.zone = system;
        zone.tzid = system.id();
    }
    return zone;
}

static Zone zoneOf(icalproperty *property, icalcomponent *event)
{
    Zone zone;
    icalparameter *parameter = icalproperty_get_first_parameter(property, ICAL_TZID_PARAMETER);
    if (!parameter)
        return zone;

    zone.tzid = icalparameter_get_tzid(parameter);
    zone.zone = TimeZones::fromTzid(zone.tzid);
    if (zone.zone.isValid()) {
        zone.kind = Zone::Named;
        return zone;
    }

    icalcomponent *calendar = event ? icalcomponent_get_parent(event) : nullptr;
    zone.custom = calendar ? icalcomponent_get_timezone(calendar, zone.tzid.constData()) : nullptr;
    // A TZID nothing defines leaves the time floating.
    zone.kind = zone.custom ? Zone::Custom : Zone::Floating;
    return zone;
}

static Time makeTime(icaltimetype value, icalproperty *property, icalcomponent *event)
{
    Time time;
    time.wall = value;
    time.wall.zone = nullptr;
    if (value.is_date)
        return time;
    if (icaltime_is_utc(value))
        time.zone.kind = Zone::Utc;
    else
        time.zone = zoneOf(property, event);
    return time;
}

static Time propertyTime(icalcomponent *event, icalproperty_kind kind)
{
    icalproperty *property = icalcomponent_get_first_property(event, kind);
    if (!property)
        return {};

    icalvalue *value = icalproperty_get_value(property);
    if (!value)
        return {};
    switch (icalvalue_isa(value)) {
    case ICAL_DATE_VALUE:
        return makeTime(icalvalue_get_date(value), property, event);
    case ICAL_DATETIME_VALUE:
        return makeTime(icalvalue_get_datetime(value), property, event);
    default:
        return {};
    }
}

static QDate dateOf(const icaltimetype &wall)
{
    return QDate(wall.year, wall.month, wall.day);
}

static QDateTime toDateTime(const Time &time)
{
    const QDate date = dateOf(time.wall);
    const QTime clock(time.wall.hour, time.wall.minute, time.wall.second);
    switch (time.zone.kind) {
    case Zone::Utc:
        return QDateTime(date, clock, QTimeZone::UTC);
    case Zone::Named:
        return QDateTime(date, clock, time.zone.zone);
    case Zone::Custom: {
        icaltimetype wall = time.wall;
        return QDateTime::fromSecsSinceEpoch(icaltime_as_timet_with_zone(wall, time.zone.custom));
    }
    case Zone::Floating:
        break;
    }
    return QDateTime(date, clock);
}

static icaltimetype dateWall(QDate date)
{
    icaltimetype wall = icaltime_null_time();
    wall.year = date.year();
    wall.month = date.month();
    wall.day = date.day();
    wall.is_date = 1;
    return wall;
}

static icaltimetype toWall(const QDateTime &instant, const Zone &zone)
{
    QDateTime local;
    switch (zone.kind) {
    case Zone::Utc:
        local = instant.toUTC();
        break;
    case Zone::Named:
        local = instant.toTimeZone(zone.zone);
        break;
    case Zone::Custom: {
        icaltimetype wall = icaltime_from_timet_with_zone(instant.toSecsSinceEpoch(), 0, zone.custom);
        wall.zone = nullptr;
        return wall;
    }
    case Zone::Floating:
        local = instant.toLocalTime();
        break;
    }

    icaltimetype wall = icaltime_null_time();
    wall.year = local.date().year();
    wall.month = local.date().month();
    wall.day = local.date().day();
    wall.hour = local.time().hour();
    wall.minute = local.time().minute();
    wall.second = local.time().second();
    return wall;
}

static void setTimeValue(icalproperty *property, const Time &time)
{
    icalproperty_remove_parameter_by_kind(property, ICAL_TZID_PARAMETER);
    icaltimetype value = time.wall;
    value.zone = nullptr;
    if (value.is_date) {
        icalproperty_set_value(property, icalvalue_new_date(value));
        return;
    }

    if (time.zone.kind == Zone::Utc)
        value.zone = icaltimezone_get_utc_timezone();
    icalproperty_set_value(property, icalvalue_new_datetime(value));
    if (time.zone.kind == Zone::Named || time.zone.kind == Zone::Custom)
        icalproperty_add_parameter(property, icalparameter_new_tzid(time.zone.tzid.constData()));
}

static icalproperty *newTimeProperty(icalproperty_kind kind, const Time &time)
{
    icalproperty *property = icalproperty_new(kind);
    setTimeValue(property, time);
    return property;
}

static void removeProperties(icalcomponent *component, icalproperty_kind kind)
{
    while (icalproperty *property = icalcomponent_get_first_property(component, kind)) {
        icalcomponent_remove_property(component, property);
        icalproperty_free(property);
    }
}

static QString text(icalcomponent *event, icalproperty_kind kind)
{
    icalproperty *property = icalcomponent_get_first_property(event, kind);
    if (!property)
        return {};
    icalvalue *value = icalproperty_get_value(property);
    const char *string = value ? icalvalue_get_text(value) : nullptr;
    return string ? QString::fromUtf8(string) : QString();
}

static void setText(icalcomponent *event, icalproperty_kind kind, const QString &value)
{
    removeProperties(event, kind);
    if (value.isEmpty() && kind != ICAL_SUMMARY_PROPERTY)
        return;
    icalproperty *property = icalproperty_new(kind);
    icalproperty_set_value(property, icalvalue_new_text(value.toUtf8().constData()));
    icalcomponent_add_property(event, property);
}

static QString uidOf(icalcomponent *event)
{
    const char *uid = icalcomponent_get_uid(event);
    return uid ? QString::fromUtf8(uid) : QString();
}

static bool isOverride(icalcomponent *event)
{
    return icalcomponent_get_first_property(event, ICAL_RECURRENCEID_PROPERTY) != nullptr;
}

static bool isCancelled(icalcomponent *event)
{
    return icalcomponent_get_first_property(event, ICAL_STATUS_PROPERTY)
            && icalcomponent_get_status(event) == ICAL_STATUS_CANCELLED;
}

static Series seriesOf(icalcomponent *event)
{
    Series series;
    series.start = propertyTime(event, ICAL_DTSTART_PROPERTY);
    series.allDay = series.start.isDate();

    const Time end = propertyTime(event, ICAL_DTEND_PROPERTY);
    icalproperty *duration = icalcomponent_get_first_property(event, ICAL_DURATION_PROPERTY);
    if (series.allDay) {
        if (!end.isNull())
            series.days = dateOf(series.start.wall).daysTo(dateOf(end.wall));
        else if (duration)
            series.days = icaldurationtype_as_int(icalproperty_get_duration(duration)) / 86400;
        series.days = qMax(1, series.days);
    } else {
        if (!end.isNull())
            series.seconds = toDateTime(series.start).secsTo(toDateTime(end));
        else if (duration)
            series.seconds = icaldurationtype_as_int(icalproperty_get_duration(duration));
        series.seconds = qMax<qint64>(0, series.seconds);
    }
    return series;
}

static QString dateKey(QDate date)
{
    return QLatin1Char('D') + date.toString(QStringLiteral("yyyyMMdd"));
}

static QString instantKey(const QDateTime &instant)
{
    return QLatin1Char('T') + QString::number(instant.toSecsSinceEpoch());
}

static QString keyOf(const Time &time)
{
    return time.isDate() ? dateKey(dateOf(time.wall)) : instantKey(toDateTime(time));
}

static bool keyIsDate(const QString &key)
{
    return key.startsWith(QLatin1Char('D'));
}

static QDate keyDate(const QString &key)
{
    return QDate::fromString(key.mid(1), QStringLiteral("yyyyMMdd"));
}

static QDateTime keyInstant(const QString &key)
{
    return QDateTime::fromSecsSinceEpoch(key.mid(1).toLongLong());
}

static bool keyBefore(const QString &a, const QString &b)
{
    if (keyIsDate(a) && keyIsDate(b))
        return keyDate(a) < keyDate(b);
    if (!keyIsDate(a) && !keyIsDate(b))
        return keyInstant(a) < keyInstant(b);
    const QDate first = keyIsDate(a) ? keyDate(a) : keyInstant(a).toLocalTime().date();
    const QDate second = keyIsDate(b) ? keyDate(b) : keyInstant(b).toLocalTime().date();
    return first < second;
}

// The instance a key names, as the series' start property writes it.
static Time instanceTime(const Series &series, const QString &key)
{
    Time time;
    time.zone = series.start.zone;
    if (series.allDay)
        time.wall = dateWall(keyIsDate(key) ? keyDate(key) : keyInstant(key).toLocalTime().date());
    else
        time.wall = toWall(keyIsDate(key) ? QDateTime(keyDate(key), toDateTime(series.start).time())
                                          : keyInstant(key), series.start.zone);
    return time;
}

static void fillDays(Occurrence *occurrence)
{
    if (occurrence->allDay)
        return;
    occurrence->firstDay = occurrence->start.date();
    occurrence->lastDay = occurrence->end > occurrence->start ? occurrence->end.addSecs(-1).date()
                                                              : occurrence->firstDay;
}

// The occurrence the event's own fields describe, starting at `start`.
static Occurrence makeOccurrence(icalcomponent *event, const Series &series, const Time &start)
{
    Occurrence occurrence;
    occurrence.summary = text(event, ICAL_SUMMARY_PROPERTY);
    occurrence.location = text(event, ICAL_LOCATION_PROPERTY);
    occurrence.allDay = series.allDay;
    if (series.allDay) {
        occurrence.firstDay = dateOf(start.wall);
        occurrence.lastDay = occurrence.firstDay.addDays(series.days - 1);
    } else {
        occurrence.start = toDateTime(start).toLocalTime();
        occurrence.end = occurrence.start.addSecs(series.seconds);
        fillDays(&occurrence);
    }
    return occurrence;
}

static bool touches(const Occurrence &occurrence, QDate from, QDate to)
{
    return occurrence.firstDay < to && occurrence.lastDay >= from;
}

static Repeat repeatOf(icalcomponent *event, const Series &series)
{
    Repeat repeat;
    icalproperty *property = icalcomponent_get_first_property(event, ICAL_RRULE_PROPERTY);
    if (!property)
        return repeat;

    const icalrecurrencetype rule = icalproperty_get_rrule(property);
    const auto length = [](const short *array, int size) {
        int count = 0;
        while (count < size && array[count] != ICAL_RECURRENCE_ARRAY_MAX)
            ++count;
        return count;
    };
    const int days = length(rule.by_day, ICAL_BY_DAY_SIZE);
    const int monthDays = length(rule.by_month_day, ICAL_BY_MONTHDAY_SIZE);
    const int months = length(rule.by_month, ICAL_BY_MONTH_SIZE);
    const bool otherParts = length(rule.by_second, ICAL_BY_SECOND_SIZE) || length(rule.by_minute, ICAL_BY_MINUTE_SIZE)
            || length(rule.by_hour, ICAL_BY_HOUR_SIZE) || length(rule.by_year_day, ICAL_BY_YEARDAY_SIZE)
            || length(rule.by_week_no, ICAL_BY_WEEKNO_SIZE) || length(rule.by_set_pos, ICAL_BY_SETPOS_SIZE);

    const QDate start = dateOf(series.start.wall);
    // iCalendar counts weekdays from Sunday = 1.
    const short startDay = short(start.dayOfWeek() % 7 + 1);
    const bool weekdays = days == 5 && [&rule] {
        for (int i = 0; i < 5; ++i) {
            if (rule.by_day[i] != ICAL_MONDAY_WEEKDAY + i)
                return false;
        }
        return true;
    }();

    repeat.frequency = Repeat::Custom;
    if (rule.interval <= 1 && !otherParts && !monthDays && !months) {
        if (rule.freq == ICAL_DAILY_RECURRENCE && !days)
            repeat.frequency = Repeat::Daily;
        else if ((rule.freq == ICAL_DAILY_RECURRENCE || rule.freq == ICAL_WEEKLY_RECURRENCE) && weekdays)
            repeat.frequency = Repeat::Weekdays;
        else if (rule.freq == ICAL_WEEKLY_RECURRENCE && (!days || (days == 1 && rule.by_day[0] == startDay)))
            repeat.frequency = Repeat::Weekly;
    }
    if (rule.interval <= 1 && !otherParts && !days) {
        if (rule.freq == ICAL_MONTHLY_RECURRENCE && !months
                && (!monthDays || (monthDays == 1 && rule.by_month_day[0] == start.day())))
            repeat.frequency = Repeat::Monthly;
        else if (rule.freq == ICAL_YEARLY_RECURRENCE
                 && (!months || (months == 1 && rule.by_month[0] == start.month()))
                 && (!monthDays || (monthDays == 1 && rule.by_month_day[0] == start.day())))
            repeat.frequency = Repeat::Yearly;
    }
    if (icalcomponent_get_first_property(event, ICAL_RDATE_PROPERTY)
            || properties(event, ICAL_RRULE_PROPERTY).size() > 1)
        repeat.frequency = Repeat::Custom;

    repeat.count = rule.count;
    if (!icaltime_is_null_time(rule.until)) {
        Time until;
        until.wall = rule.until;
        until.wall.zone = nullptr;
        if (rule.until.is_date)
            repeat.until = dateOf(rule.until);
        else {
            until.zone = icaltime_is_utc(rule.until) ? utcZone() : series.start.zone;
            repeat.until = toDateTime(until).toLocalTime().date();
        }
    }
    return repeat;
}

static icalrecurrencetype ruleFor(const Repeat &repeat, const Series &series)
{
    icalrecurrencetype rule;
    icalrecurrencetype_clear(&rule);
    switch (repeat.frequency) {
    case Repeat::Daily:
        rule.freq = ICAL_DAILY_RECURRENCE;
        break;
    case Repeat::Weekdays:
        rule.freq = ICAL_WEEKLY_RECURRENCE;
        for (int i = 0; i < 5; ++i)
            rule.by_day[i] = short(ICAL_MONDAY_WEEKDAY + i);
        rule.by_day[5] = ICAL_RECURRENCE_ARRAY_MAX;
        break;
    case Repeat::Weekly:
        rule.freq = ICAL_WEEKLY_RECURRENCE;
        break;
    case Repeat::Monthly:
        rule.freq = ICAL_MONTHLY_RECURRENCE;
        break;
    case Repeat::Yearly:
        rule.freq = ICAL_YEARLY_RECURRENCE;
        break;
    case Repeat::None:
    case Repeat::Custom:
        break;
    }
    rule.interval = 1;

    if (repeat.count > 0) {
        rule.count = repeat.count;
    } else if (repeat.until.isValid()) {
        if (series.allDay) {
            rule.until = dateWall(repeat.until);
        } else {
            // The last day is included; a time with a zone ends in UTC.
            const QDateTime end(repeat.until, QTime(23, 59, 59));
            Time until;
            until.zone = series.start.zone.kind == Zone::Floating ? Zone() : utcZone();
            until.wall = toWall(end, until.zone);
            rule.until = until.wall;
            if (until.zone.kind == Zone::Utc)
                rule.until.zone = icaltimezone_get_utc_timezone();
        }
    }
    return rule;
}

// The start of every instance a rule or RDATE makes, in the series' zone;
// stops at the first one that starts at or after `end`.
static void instances(icalcomponent *event, const Series &series, const QDateTime &skipTo, const QDateTime &end,
                      const std::function<bool(const Time &)> &visit)
{
    for (icalproperty *property : properties(event, ICAL_RRULE_PROPERTY)) {
        icalrecurrencetype rule = icalproperty_get_rrule(property);
        // Compared with floating instances below, so it has to be one too.
        if (!icaltime_is_null_time(rule.until)) {
            if (series.allDay) {
                if (!rule.until.is_date) {
                    Time until;
                    until.wall = rule.until;
                    until.wall.zone = nullptr;
                    until.zone = icaltime_is_utc(rule.until) ? utcZone() : series.start.zone;
                    rule.until = dateWall(toDateTime(until).toLocalTime().date());
                }
            } else if (rule.until.is_date) {
                rule.until.is_date = 0;
                rule.until.hour = 23;
                rule.until.minute = 59;
                rule.until.second = 59;
            } else if (icaltime_is_utc(rule.until)) {
                Time until;
                until.wall = rule.until;
                until.wall.zone = nullptr;
                until.zone.kind = Zone::Utc;
                rule.until = toWall(toDateTime(until), series.start.zone);
            }
            rule.until.zone = nullptr;
        }

        icalrecur_iterator *iterator = icalrecur_iterator_new(rule, series.start.wall);
        if (!iterator)
            continue;
        if (rule.count == 0 && skipTo.isValid()) {
            const icaltimetype skip = series.allDay ? dateWall(skipTo.toLocalTime().date())
                                                    : toWall(skipTo, series.start.zone);
            if (icaltime_compare(skip, series.start.wall) > 0 && !icalrecur_iterator_set_start(iterator, skip)) {
                icalrecur_iterator_free(iterator);
                iterator = icalrecur_iterator_new(rule, series.start.wall);
            }
        }

        for (int i = 0; i < MaximumInstances; ++i) {
            const icaltimetype next = icalrecur_iterator_next(iterator);
            if (icaltime_is_null_time(next))
                break;
            Time time;
            time.wall = next;
            time.wall.zone = nullptr;
            time.zone = series.start.zone;
            const QDateTime start = series.allDay ? QDateTime(dateOf(next), QTime(0, 0)) : toDateTime(time);
            if (start >= end || !visit(time))
                break;
        }
        icalrecur_iterator_free(iterator);
    }

    for (icalproperty *property : properties(event, ICAL_RDATE_PROPERTY)) {
        const icaldatetimeperiodtype value = icalproperty_get_rdate(property);
        Time time = makeTime(icaltime_is_null_time(value.time) ? value.period.start : value.time, property, event);
        if (series.allDay != time.isDate())
            continue;
        if (!visit(time))
            break;
    }
}

static QSet<QString> exclusions(icalcomponent *event)
{
    QSet<QString> keys;
    for (icalproperty *property : properties(event, ICAL_EXDATE_PROPERTY)) {
        keys.insert(keyOf(makeTime(icalproperty_get_exdate(property), property, event)));
    }
    return keys;
}

static bool excluded(const QSet<QString> &exclusions, const Time &time)
{
    return exclusions.contains(keyOf(time))
            || (!time.isDate() && exclusions.contains(dateKey(dateOf(time.wall))));
}

static void addTimezone(icalcomponent *calendar, const Zone &zone)
{
    if (zone.kind != Zone::Named || icalcomponent_get_timezone(calendar, zone.tzid.constData()))
        return;
    icaltimezone *builtin = icaltimezone_get_builtin_timezone(zone.zone.id().constData());
    icalcomponent *definition = builtin ? icaltimezone_get_component(builtin) : nullptr;
    if (!definition)
        return;
    // The builtin zones carry a vendor prefix; the events name the zone without it.
    icalcomponent *copy = icalcomponent_new_clone(definition);
    removeProperties(copy, ICAL_TZID_PROPERTY);
    icalcomponent_add_property(copy, icalproperty_new_tzid(zone.tzid.constData()));
    icalcomponent_add_component(calendar, copy);
}

static void writeAlarm(icalcomponent *event, const EventData &data)
{
    if (data.alarm == EventData::CustomAlarm)
        return;
    while (icalcomponent *alarm = icalcomponent_get_first_component(event, ICAL_VALARM_COMPONENT)) {
        icalcomponent_remove_component(event, alarm);
        icalcomponent_free(alarm);
    }
    if (data.alarm != EventData::OffsetAlarm)
        return;

    icalcomponent *alarm = icalcomponent_new(ICAL_VALARM_COMPONENT);
    icalcomponent_add_property(alarm, icalproperty_new_action(ICAL_ACTION_DISPLAY));
    icalcomponent_add_property(alarm, icalproperty_new_description(data.summary.toUtf8().constData()));
    icaltriggertype trigger;
    trigger.time = icaltime_null_time();
    trigger.duration = icaldurationtype_from_int(data.alarmOffset * 60);
    icalcomponent_add_property(alarm, icalproperty_new_trigger(trigger));
    icalcomponent_add_component(event, alarm);
}

static void readAlarm(icalcomponent *event, EventData *data)
{
    icalcomponent *alarm = icalcomponent_get_first_component(event, ICAL_VALARM_COMPONENT);
    if (!alarm)
        return;
    data->alarm = EventData::CustomAlarm;
    if (components(event, ICAL_VALARM_COMPONENT).size() > 1)
        return;

    icalproperty *property = icalcomponent_get_first_property(alarm, ICAL_TRIGGER_PROPERTY);
    if (!property)
        return;
    icalparameter *related = icalproperty_get_first_parameter(property, ICAL_RELATED_PARAMETER);
    const icaltriggertype trigger = icalproperty_get_trigger(property);
    if (!icaltime_is_null_time(trigger.time) || (related && icalparameter_get_related(related) == ICAL_RELATED_END))
        return;
    const int seconds = icaldurationtype_as_int(trigger.duration);
    if (seconds % 60)
        return;
    data->alarm = EventData::OffsetAlarm;
    data->alarmOffset = seconds / 60;
}

static QList<QDateTime> alarmTimes(icalcomponent *event, const Occurrence &occurrence)
{
    QList<QDateTime> times;
    const QDateTime start = occurrence.allDay ? QDateTime(occurrence.firstDay, QTime(0, 0)) : occurrence.start;
    const QDateTime end = occurrence.allDay ? QDateTime(occurrence.lastDay.addDays(1), QTime(0, 0)) : occurrence.end;
    for (icalcomponent *alarm : components(event, ICAL_VALARM_COMPONENT)) {
        icalproperty *property = icalcomponent_get_first_property(alarm, ICAL_TRIGGER_PROPERTY);
        if (!property)
            continue;
        const icaltriggertype trigger = icalproperty_get_trigger(property);
        if (!icaltime_is_null_time(trigger.time)) {
            times.append(toDateTime(makeTime(trigger.time, property, event)).toLocalTime());
            continue;
        }
        icalparameter *related = icalproperty_get_first_parameter(property, ICAL_RELATED_PARAMETER);
        const bool fromEnd = related && icalparameter_get_related(related) == ICAL_RELATED_END;
        times.append((fromEnd ? end : start).addSecs(icaldurationtype_as_int(trigger.duration)));
    }
    return times;
}

// How far before or after its occurrence any alarm of the file goes off, in days.
static int alarmReach(icalcomponent *calendar)
{
    int reach = 0;
    for (icalcomponent *event : components(calendar, ICAL_VEVENT_COMPONENT)) {
        for (icalcomponent *alarm : components(event, ICAL_VALARM_COMPONENT)) {
            icalproperty *property = icalcomponent_get_first_property(alarm, ICAL_TRIGGER_PROPERTY);
            if (property) {
                const int seconds = icaldurationtype_as_int(icalproperty_get_trigger(property).duration);
                reach = qMax(reach, qAbs(seconds) / 86400 + 1);
            }
        }
    }
    return reach;
}

static void shiftTime(icalproperty *property, icalcomponent *event, qint64 seconds, int days)
{
    Time time;
    icalvalue *value = icalproperty_get_value(property);
    if (!value)
        return;
    time = makeTime(icalvalue_isa(value) == ICAL_DATE_VALUE ? icalvalue_get_date(value) : icalvalue_get_datetime(value),
                    property, event);
    if (time.isDate())
        time.wall = dateWall(dateOf(time.wall).addDays(days));
    else
        time.wall = toWall(toDateTime(time).addSecs(seconds), time.zone);
    setTimeValue(property, time);
}

EventFile::EventFile(icalcomponent *calendar)
    : m_calendar(calendar)
{
}

EventFile::~EventFile()
{
    if (m_calendar)
        icalcomponent_free(m_calendar);
}

static icalcomponent *newCalendar()
{
    icalcomponent *calendar = icalcomponent_new(ICAL_VCALENDAR_COMPONENT);
    icalcomponent_add_property(calendar, icalproperty_new_version("2.0"));
    icalcomponent_add_property(calendar, icalproperty_new_prodid("-//Cutefish//Calendar//EN"));
    return calendar;
}

std::unique_ptr<EventFile> EventFile::fromData(const QByteArray &data)
{
    icalcomponent *root = icalparser_parse_string(data.constData());
    if (!root)
        return nullptr;

    if (icalcomponent_isa(root) == ICAL_VEVENT_COMPONENT) {
        icalcomponent *calendar = newCalendar();
        icalcomponent_add_component(calendar, root);
        root = calendar;
    }
    if (icalcomponent_isa(root) != ICAL_VCALENDAR_COMPONENT
            || !icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT)) {
        icalcomponent_free(root);
        return nullptr;
    }
    return std::unique_ptr<EventFile>(new EventFile(root));
}

std::vector<std::unique_ptr<EventFile>> EventFile::split(const QByteArray &data)
{
    std::vector<std::unique_ptr<EventFile>> files;
    icalcomponent *root = icalparser_parse_string(data.constData());
    if (!root)
        return files;

    QList<icalcomponent *> calendars;
    if (icalcomponent_isa(root) == ICAL_XROOT_COMPONENT) {
        for (icalcomponent *calendar : components(root, ICAL_VCALENDAR_COMPONENT))
            calendars.append(calendar);
    } else if (icalcomponent_isa(root) == ICAL_VCALENDAR_COMPONENT) {
        calendars.append(root);
    }

    QHash<QString, int> byUid;
    for (icalcomponent *calendar : std::as_const(calendars)) {
        for (icalcomponent *event : components(calendar, ICAL_VEVENT_COMPONENT)) {
            if (!icalcomponent_get_first_property(event, ICAL_DTSTART_PROPERTY))
                continue;
            QString uid = uidOf(event);
            icalcomponent *copy = icalcomponent_new_clone(event);
            if (uid.isEmpty()) {
                uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
                icalcomponent_set_uid(copy, uid.toUtf8().constData());
            }

            auto it = byUid.constFind(uid);
            if (it == byUid.constEnd()) {
                icalcomponent *target = newCalendar();
                // The zones it may name; unused ones cost nothing but bytes.
                for (icalcomponent *zone : components(calendar, ICAL_VTIMEZONE_COMPONENT))
                    icalcomponent_add_component(target, icalcomponent_new_clone(zone));
                files.push_back(std::unique_ptr<EventFile>(new EventFile(target)));
                it = byUid.insert(uid, int(files.size()) - 1);
            }
            icalcomponent_add_component(files[*it]->m_calendar, copy);
        }
    }
    icalcomponent_free(root);
    return files;
}

std::unique_ptr<EventFile> EventFile::create(const EventData &data)
{
    icalcomponent *calendar = newCalendar();
    icalcomponent *event = icalcomponent_new(ICAL_VEVENT_COMPONENT);
    icalcomponent_set_uid(event, QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8().constData());
    const icaltimetype now = icaltime_current_time_with_zone(icaltimezone_get_utc_timezone());
    icalcomponent_add_property(event, icalproperty_new_dtstamp(now));
    icalcomponent_add_property(event, icalproperty_new_created(now));
    icalcomponent_add_property(event, icalproperty_new_lastmodified(now));
    icalcomponent_add_property(event, icalproperty_new_sequence(0));
    icalcomponent_add_component(calendar, event);

    std::unique_ptr<EventFile> file(new EventFile(calendar));
    file->setData(data);
    // A new event starts at sequence 0; setData counted it as a change.
    icalcomponent_set_sequence(event, 0);
    return file;
}

std::unique_ptr<EventFile> EventFile::clone() const
{
    return std::unique_ptr<EventFile>(new EventFile(icalcomponent_new_clone(m_calendar)));
}

QByteArray EventFile::toData() const
{
    char *string = icalcomponent_as_ical_string_r(m_calendar);
    const QByteArray data(string);
    icalmemory_free_buffer(string);
    return data;
}

QByteArray EventFile::join(const QList<const EventFile *> &files)
{
    icalcomponent *calendar = newCalendar();
    QSet<QByteArray> zones;
    for (const EventFile *file : files) {
        for (icalcomponent *component : components(file->m_calendar, ICAL_ANY_COMPONENT)) {
            if (icalcomponent_isa(component) == ICAL_VTIMEZONE_COMPONENT) {
                icalproperty *tzid = icalcomponent_get_first_property(component, ICAL_TZID_PROPERTY);
                const QByteArray id = tzid ? QByteArray(icalproperty_get_tzid(tzid)) : QByteArray();
                if (zones.contains(id))
                    continue;
                zones.insert(id);
            } else if (icalcomponent_isa(component) != ICAL_VEVENT_COMPONENT) {
                continue;
            }
            icalcomponent_add_component(calendar, icalcomponent_new_clone(component));
        }
    }
    char *string = icalcomponent_as_ical_string_r(calendar);
    const QByteArray data(string);
    icalmemory_free_buffer(string);
    icalcomponent_free(calendar);
    return data;
}

icalcomponent *EventFile::master() const
{
    icalcomponent *first = nullptr;
    for (icalcomponent *event : components(m_calendar, ICAL_VEVENT_COMPONENT)) {
        if (!isOverride(event))
            return event;
        if (!first)
            first = event;
    }
    return first;
}

icalcomponent *EventFile::override(const QString &recurrenceKey) const
{
    if (recurrenceKey.isEmpty())
        return nullptr;
    for (icalcomponent *event : components(m_calendar, ICAL_VEVENT_COMPONENT)) {
        if (isOverride(event) && keyOf(propertyTime(event, ICAL_RECURRENCEID_PROPERTY)) == recurrenceKey)
            return event;
    }
    return nullptr;
}

void EventFile::touch(icalcomponent *event) const
{
    const icaltimetype now = icaltime_current_time_with_zone(icaltimezone_get_utc_timezone());
    removeProperties(event, ICAL_DTSTAMP_PROPERTY);
    removeProperties(event, ICAL_LASTMODIFIED_PROPERTY);
    icalcomponent_add_property(event, icalproperty_new_dtstamp(now));
    icalcomponent_add_property(event, icalproperty_new_lastmodified(now));
    const bool sequenced = icalcomponent_get_first_property(event, ICAL_SEQUENCE_PROPERTY);
    icalcomponent_set_sequence(event, sequenced ? icalcomponent_get_sequence(event) + 1 : 1);
}

QString EventFile::uid() const
{
    icalcomponent *event = master();
    return event ? uidOf(event) : QString();
}

QString EventFile::summary() const
{
    icalcomponent *event = master();
    return event ? text(event, ICAL_SUMMARY_PROPERTY) : QString();
}

QString EventFile::location() const
{
    icalcomponent *event = master();
    return event ? text(event, ICAL_LOCATION_PROPERTY) : QString();
}

QString EventFile::description() const
{
    icalcomponent *event = master();
    return event ? text(event, ICAL_DESCRIPTION_PROPERTY) : QString();
}

bool EventFile::isRecurring() const
{
    icalcomponent *event = master();
    return event && !isOverride(event)
            && (icalcomponent_get_first_property(event, ICAL_RRULE_PROPERTY)
                || icalcomponent_get_first_property(event, ICAL_RDATE_PROPERTY));
}

void EventFile::expand(QDate from, QDate to, QList<Occurrence> *out) const
{
    icalcomponent *event = master();
    if (!event)
        return;
    const Series series = seriesOf(event);
    if (series.start.isNull())
        return;

    if (!isRecurring()) {
        // An instance on its own, as a calendar without its series sends it.
        if (isOverride(event) && isCancelled(event))
            return;
        Occurrence occurrence = makeOccurrence(event, series, series.start);
        if (touches(occurrence, from, to))
            out->append(occurrence);
        return;
    }

    QHash<QString, icalcomponent *> overrides;
    for (icalcomponent *other : components(m_calendar, ICAL_VEVENT_COMPONENT)) {
        if (isOverride(other))
            overrides.insert(keyOf(propertyTime(other, ICAL_RECURRENCEID_PROPERTY)), other);
    }
    const QSet<QString> excludedKeys = exclusions(event);

    const QDateTime rangeStart(from, QTime(0, 0));
    const QDateTime rangeEnd(to, QTime(0, 0));
    // Instances that start before the range may still reach into it.
    const QDateTime skipTo = series.allDay ? rangeStart.addDays(-series.days - 1)
                                           : rangeStart.addSecs(-series.seconds).addDays(-1);

    instances(event, series, skipTo, rangeEnd, [&](const Time &time) {
        const QString key = keyOf(time);
        if (overrides.contains(key) || excluded(excludedKeys, time))
            return true;
        Occurrence occurrence = makeOccurrence(event, series, time);
        occurrence.recurrenceKey = key;
        occurrence.recurring = true;
        if (touches(occurrence, from, to))
            out->append(occurrence);
        return true;
    });

    // Changed instances appear where they were moved to, whatever range they came from.
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        if (isCancelled(it.value()) || excludedKeys.contains(it.key()))
            continue;
        const Series moved = seriesOf(it.value());
        if (moved.start.isNull())
            continue;
        Occurrence occurrence = makeOccurrence(it.value(), moved, moved.start);
        occurrence.recurrenceKey = it.key();
        occurrence.recurring = true;
        if (touches(occurrence, from, to))
            out->append(occurrence);
    }
}

bool EventFile::nextOccurrence(const QDateTime &after, int within, Occurrence *out) const
{
    QList<Occurrence> occurrences;
    const QDate first = after.toLocalTime().date();
    expand(first, isRecurring() ? first.addDays(within) : QDate(9999, 12, 31), &occurrences);

    const Occurrence *best = nullptr;
    QDateTime bestStart;
    for (const Occurrence &occurrence : std::as_const(occurrences)) {
        const QDateTime start = occurrence.allDay ? QDateTime(occurrence.firstDay, QTime(0, 0)) : occurrence.start;
        if (start < after && !(occurrence.allDay && occurrence.firstDay == first))
            continue;
        if (!best || start < bestStart) {
            best = &occurrence;
            bestStart = start;
        }
    }
    if (best)
        *out = *best;
    return best;
}

QList<QPair<QDateTime, Occurrence>> EventFile::alarms(const QDateTime &from, const QDateTime &to) const
{
    QList<QPair<QDateTime, Occurrence>> result;
    const QList<icalcomponent *> events = components(m_calendar, ICAL_VEVENT_COMPONENT);
    if (std::none_of(events.cbegin(), events.cend(), [](icalcomponent *event) {
            return icalcomponent_get_first_component(event, ICAL_VALARM_COMPONENT) != nullptr;
        }))
        return result;

    const int reach = alarmReach(m_calendar);
    QList<Occurrence> occurrences;
    expand(from.toLocalTime().date().addDays(-reach - 1), to.toLocalTime().date().addDays(reach + 1), &occurrences);
    for (const Occurrence &occurrence : std::as_const(occurrences)) {
        icalcomponent *event = occurrence.recurrenceKey.isEmpty() ? master() : override(occurrence.recurrenceKey);
        if (!event)
            event = master();
        const QList<QDateTime> times = alarmTimes(event, occurrence);
        for (const QDateTime &time : times) {
            if (time >= from && time < to)
                result.append({ time, occurrence });
        }
    }
    return result;
}

EventData EventFile::data(const QString &recurrenceKey) const
{
    EventData data;
    icalcomponent *event = master();
    if (!event)
        return data;

    icalcomponent *changed = override(recurrenceKey);
    icalcomponent *source = changed ? changed : event;
    data.summary = text(source, ICAL_SUMMARY_PROPERTY);
    data.location = text(source, ICAL_LOCATION_PROPERTY);
    data.description = text(source, ICAL_DESCRIPTION_PROPERTY);
    readAlarm(source, &data);

    const Series series = seriesOf(event);
    data.repeat = isRecurring() ? repeatOf(event, series) : Repeat();

    const Series timing = changed ? seriesOf(changed) : series;
    const Time start = changed || recurrenceKey.isEmpty() ? timing.start : instanceTime(series, recurrenceKey);
    data.allDay = timing.allDay;
    if (timing.allDay) {
        data.startDate = dateOf(start.wall);
        data.endDate = data.startDate.addDays(timing.days - 1);
    } else {
        data.start = toDateTime(start).toLocalTime();
        data.end = data.start.addSecs(timing.seconds);
    }
    return data;
}

// Writes the fields every event has; the zone of a timed start stays as it was.
static void writeFields(icalcomponent *calendar, icalcomponent *event, const EventData &data)
{
    setText(event, ICAL_SUMMARY_PROPERTY, data.summary);
    setText(event, ICAL_LOCATION_PROPERTY, data.location);
    setText(event, ICAL_DESCRIPTION_PROPERTY, data.description);

    const Time old = propertyTime(event, ICAL_DTSTART_PROPERTY);
    const Zone zone = old.isNull() || old.isDate() ? systemZone() : old.zone;
    removeProperties(event, ICAL_DTSTART_PROPERTY);
    removeProperties(event, ICAL_DTEND_PROPERTY);
    removeProperties(event, ICAL_DURATION_PROPERTY);

    Time start;
    Time end;
    if (data.allDay) {
        start.wall = dateWall(data.startDate);
        end.wall = dateWall(qMax(data.endDate, data.startDate).addDays(1));
    } else {
        start.zone = zone;
        end.zone = zone;
        start.wall = toWall(data.start, zone);
        end.wall = toWall(qMax(data.end, data.start), zone);
        addTimezone(calendar, zone);
    }
    icalcomponent_add_property(event, newTimeProperty(ICAL_DTSTART_PROPERTY, start));
    icalcomponent_add_property(event, newTimeProperty(ICAL_DTEND_PROPERTY, end));
    writeAlarm(event, data);
}

void EventFile::setData(const EventData &data)
{
    icalcomponent *event = master();
    if (!event)
        return;

    const Series before = seriesOf(event);
    const Repeat oldRepeat = isRecurring() ? repeatOf(event, before) : Repeat();
    writeFields(m_calendar, event, data);
    const Series after = seriesOf(event);

    const bool kindChanged = before.allDay != after.allDay;
    const bool ruleChanged = data.repeat.frequency != Repeat::Custom && !(data.repeat == oldRepeat);
    // A new end alone keeps the same occurrences, and so their changes and removals.
    const bool frequencyChanged = ruleChanged && data.repeat.frequency != oldRepeat.frequency;
    if (ruleChanged) {
        removeProperties(event, ICAL_RRULE_PROPERTY);
        if (data.repeat.frequency == Repeat::None)
            removeProperties(event, ICAL_RDATE_PROPERTY);
        else
            icalcomponent_add_property(event, icalproperty_new_rrule(ruleFor(data.repeat, after)));
    }
    if (!frequencyChanged && !kindChanged && !before.start.isNull()) {
        // The instances moved with the start: so do the ones that were changed or removed.
        const qint64 seconds = before.allDay ? 0 : toDateTime(before.start).secsTo(toDateTime(after.start));
        const int days = before.allDay ? dateOf(before.start.wall).daysTo(dateOf(after.start.wall)) : 0;
        if (seconds || days) {
            for (icalproperty *property : properties(event, ICAL_EXDATE_PROPERTY))
                shiftTime(property, event, seconds, days);
            for (icalcomponent *other : components(m_calendar, ICAL_VEVENT_COMPONENT)) {
                if (!isOverride(other))
                    continue;
                for (icalproperty_kind kind : { ICAL_RECURRENCEID_PROPERTY, ICAL_DTSTART_PROPERTY, ICAL_DTEND_PROPERTY }) {
                    if (icalproperty *property = icalcomponent_get_first_property(other, kind))
                        shiftTime(property, other, seconds, days);
                }
            }
        }
    }

    // Instances of another rule, or of another kind of start, are not these instances.
    if (frequencyChanged || kindChanged) {
        removeProperties(event, ICAL_EXDATE_PROPERTY);
        QList<icalcomponent *> stale;
        for (icalcomponent *other : components(m_calendar, ICAL_VEVENT_COMPONENT)) {
            if (isOverride(other))
                stale.append(other);
        }
        for (icalcomponent *other : std::as_const(stale)) {
            icalcomponent_remove_component(m_calendar, other);
            icalcomponent_free(other);
        }
    }
    if (kindChanged && !ruleChanged && icalcomponent_get_first_property(event, ICAL_RRULE_PROPERTY)) {
        // UNTIL has to be a date for dates and a time for times.
        const Repeat repeat = repeatOf(event, before);
        icalproperty *property = icalcomponent_get_first_property(event, ICAL_RRULE_PROPERTY);
        icalrecurrencetype rule = icalproperty_get_rrule(property);
        if (repeat.until.isValid()) {
            Repeat ending;
            ending.until = repeat.until;
            ending.frequency = Repeat::Daily;
            rule.until = ruleFor(ending, after).until;
            icalproperty_set_rrule(property, rule);
        }
    }
    touch(event);
}

void EventFile::setOccurrenceData(const QString &recurrenceKey, const EventData &data)
{
    icalcomponent *event = master();
    if (!event)
        return;

    icalcomponent *changed = override(recurrenceKey);
    if (!changed) {
        changed = icalcomponent_new_clone(event);
        removeProperties(changed, ICAL_RRULE_PROPERTY);
        removeProperties(changed, ICAL_RDATE_PROPERTY);
        removeProperties(changed, ICAL_EXDATE_PROPERTY);
        removeProperties(changed, ICAL_RECURRENCEID_PROPERTY);
        removeProperties(changed, ICAL_SEQUENCE_PROPERTY);
        icalcomponent_add_property(changed, newTimeProperty(ICAL_RECURRENCEID_PROPERTY,
                                                            instanceTime(seriesOf(event), recurrenceKey)));
        icalcomponent_add_component(m_calendar, changed);
    }
    EventData fields = data;
    fields.repeat = Repeat();
    writeFields(m_calendar, changed, fields);
    touch(changed);
}

void EventFile::resetOccurrence(const QString &recurrenceKey)
{
    if (icalcomponent *changed = override(recurrenceKey)) {
        icalcomponent_remove_component(m_calendar, changed);
        icalcomponent_free(changed);
    }
}

void EventFile::excludeOccurrence(const QString &recurrenceKey)
{
    icalcomponent *event = master();
    if (!event)
        return;
    if (icalcomponent *changed = override(recurrenceKey)) {
        icalcomponent_remove_component(m_calendar, changed);
        icalcomponent_free(changed);
    }
    icalcomponent_add_property(event, newTimeProperty(ICAL_EXDATE_PROPERTY,
                                                      instanceTime(seriesOf(event), recurrenceKey)));
    touch(event);
}

bool EventFile::isFirstOccurrence(const QString &recurrenceKey) const
{
    icalcomponent *event = master();
    if (!event)
        return true;
    return !keyBefore(keyOf(seriesOf(event).start), recurrenceKey);
}

static int instancesBefore(icalcomponent *event, const Series &series, const QString &recurrenceKey)
{
    int count = 0;
    instances(event, series, QDateTime(), QDateTime(QDate(9999, 12, 31), QTime(0, 0)), [&](const Time &time) {
        if (!keyBefore(keyOf(time), recurrenceKey))
            return false;
        ++count;
        return true;
    });
    return count;
}

bool EventFile::endBefore(const QString &recurrenceKey)
{
    icalcomponent *event = master();
    if (!event || isFirstOccurrence(recurrenceKey))
        return false;

    const Series series = seriesOf(event);
    Time last = series.start;
    instances(event, series, QDateTime(), QDateTime(QDate(9999, 12, 31), QTime(0, 0)), [&](const Time &time) {
        if (!keyBefore(keyOf(time), recurrenceKey))
            return false;
        last = time;
        return true;
    });
    for (icalproperty *property : properties(event, ICAL_RRULE_PROPERTY)) {
        icalrecurrencetype rule = icalproperty_get_rrule(property);
        rule.count = 0;
        if (series.allDay) {
            rule.until = last.wall;
        } else {
            const Zone zone = series.start.zone.kind == Zone::Floating ? Zone() : utcZone();
            rule.until = toWall(toDateTime(last), zone);
            if (zone.kind == Zone::Utc)
                rule.until.zone = icaltimezone_get_utc_timezone();
        }
        icalproperty_set_rrule(property, rule);
    }

    // What came after it belongs to the part that was cut off.
    const auto later = [&](icalproperty *property, icalcomponent *owner) {
        icalvalue *value = icalproperty_get_value(property);
        if (!value)
            return false;
        icaltimetype time;
        if (icalvalue_isa(value) == ICAL_PERIOD_VALUE)
            time = icalvalue_get_period(value).start;
        else
            time = icalvalue_isa(value) == ICAL_DATE_VALUE ? icalvalue_get_date(value) : icalvalue_get_datetime(value);
        return !keyBefore(keyOf(makeTime(time, property, owner)), recurrenceKey);
    };
    for (icalproperty_kind kind : { ICAL_EXDATE_PROPERTY, ICAL_RDATE_PROPERTY }) {
        QList<icalproperty *> remove;
        for (icalproperty *property : properties(event, kind)) {
            if (later(property, event))
                remove.append(property);
        }
        for (icalproperty *property : std::as_const(remove)) {
            icalcomponent_remove_property(event, property);
            icalproperty_free(property);
        }
    }
    QList<icalcomponent *> remove;
    for (icalcomponent *other : components(m_calendar, ICAL_VEVENT_COMPONENT)) {
        if (isOverride(other) && later(icalcomponent_get_first_property(other, ICAL_RECURRENCEID_PROPERTY), other))
            remove.append(other);
    }
    for (icalcomponent *other : std::as_const(remove)) {
        icalcomponent_remove_component(m_calendar, other);
        icalcomponent_free(other);
    }
    touch(event);
    return true;
}

std::unique_ptr<EventFile> EventFile::splitAt(const QString &recurrenceKey, const EventData &data)
{
    icalcomponent *event = master();
    if (!event || isFirstOccurrence(recurrenceKey))
        return nullptr;

    const Series series = seriesOf(event);
    const int before = instancesBefore(event, series, recurrenceKey);

    std::unique_ptr<EventFile> rest = clone();
    icalcomponent *restEvent = rest->master();
    const QString uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (icalcomponent *other : components(rest->m_calendar, ICAL_VEVENT_COMPONENT))
        icalcomponent_set_uid(other, uid.toUtf8().constData());

    // The rest starts where it was cut, still lasting as long; earlier
    // changes and removals stay with the first part.
    QList<icalcomponent *> earlier;
    for (icalcomponent *other : components(rest->m_calendar, ICAL_VEVENT_COMPONENT)) {
        if (isOverride(other) && keyBefore(keyOf(propertyTime(other, ICAL_RECURRENCEID_PROPERTY)), recurrenceKey))
            earlier.append(other);
    }
    for (icalcomponent *other : std::as_const(earlier)) {
        icalcomponent_remove_component(rest->m_calendar, other);
        icalcomponent_free(other);
    }
    QList<icalproperty *> removed;
    for (icalproperty *property : properties(restEvent, ICAL_EXDATE_PROPERTY)) {
        if (keyBefore(keyOf(makeTime(icalproperty_get_exdate(property), property, restEvent)), recurrenceKey))
            removed.append(property);
    }
    for (icalproperty *property : std::as_const(removed)) {
        icalcomponent_remove_property(restEvent, property);
        icalproperty_free(property);
    }

    const Time start = instanceTime(series, recurrenceKey);
    removeProperties(restEvent, ICAL_DTSTART_PROPERTY);
    removeProperties(restEvent, ICAL_DTEND_PROPERTY);
    removeProperties(restEvent, ICAL_DURATION_PROPERTY);
    Time end = start;
    if (series.allDay)
        end.wall = dateWall(dateOf(start.wall).addDays(series.days));
    else
        end.wall = toWall(toDateTime(start).addSecs(series.seconds), start.zone);
    icalcomponent_add_property(restEvent, newTimeProperty(ICAL_DTSTART_PROPERTY, start));
    icalcomponent_add_property(restEvent, newTimeProperty(ICAL_DTEND_PROPERTY, end));
    for (icalproperty *property : properties(restEvent, ICAL_RRULE_PROPERTY)) {
        icalrecurrencetype rule = icalproperty_get_rrule(property);
        if (rule.count > 0) {
            rule.count = qMax(1, rule.count - before);
            icalproperty_set_rrule(property, rule);
        }
    }
    removeProperties(restEvent, ICAL_CREATED_PROPERTY);
    removeProperties(restEvent, ICAL_SEQUENCE_PROPERTY);

    // An unchanged rule keeps what is left of a count.
    EventData fields = data;
    if (fields.repeat == repeatOf(event, series))
        fields.repeat.frequency = Repeat::Custom;
    rest->setData(fields);

    endBefore(recurrenceKey);
    return rest;
}
