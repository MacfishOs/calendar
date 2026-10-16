// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "color.h"
#include "eventfile.h"

#include <base/file_watcher.h>
#include <base/signal.h>
#include <base/thread_pool.h>
#include <base/timer.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct CalendarInfo
{
    std::string id;
    std::string name;
    Rgb color;
};

// Calendars as vdir directories: one iCalendar file per event, name and colour
// in `displayname` and `color`. Changes made by others on disk are picked up.
class CalendarStore
{
public:
    explicit CalendarStore(std::string root = defaultRoot());
    ~CalendarStore();

    CalendarStore(const CalendarStore&) = delete;
    CalendarStore& operator=(const CalendarStore&) = delete;

    static std::string defaultRoot();

    const std::string& root() const { return m_root; }
    void load();
    void loadAsync();
    bool isReady() const { return m_ready; }

    std::vector<CalendarInfo> calendars() const;
    bool hasCalendar(const std::string& id) const { return m_calendars.contains(id); }
    CalendarInfo calendar(const std::string& id) const;
    std::string createCalendar(const std::string& name, Rgb color);
    bool updateCalendar(const std::string& id, const std::string& name, Rgb color);
    // Moves the calendar's directory to the trash.
    bool removeCalendar(const std::string& id);
    int eventCount(const std::string& calendarId) const;

    std::vector<Occurrence> occurrences(Date from, Date to,
                                        const std::unordered_set<std::string>& hiddenCalendars = {}) const;
    std::vector<std::string> keys() const;
    const EventFile* event(const std::string& key) const;
    static std::string calendarOf(const std::string& key);

    std::string addEvent(const std::string& calendarId, std::unique_ptr<EventFile> file);
    std::string updateEvent(const std::string& key, std::unique_ptr<EventFile> file);
    std::string moveEvent(const std::string& key, const std::string& calendarId);
    bool removeEvent(const std::string& key);

    // Replaces events with the same UID; -1 when the data holds none.
    int importData(const std::string& calendarId, std::string_view data);
    std::string exportCalendar(const std::string& id) const;

    cutefish::base::Signal<> readyChanged;
    cutefish::base::Signal<> changed;

private:
    struct Entry
    {
        std::shared_ptr<const EventFile> file;
        std::filesystem::file_time_type modified;
        uintmax_t size = 0;
        // Days a single event touches; repeating events are always expanded.
        Date firstDay;
        Date lastDay;
        bool recurring = false;
    };
    struct Snapshot
    {
        std::unordered_map<std::string, CalendarInfo> calendars;
        std::unordered_map<std::string, Entry> events;
    };

    static Snapshot scan(const std::string& root, const std::unordered_map<std::string, Entry>& known);
    static Entry makeEntry(std::shared_ptr<const EventFile> file);
    void apply(Snapshot snapshot);
    void watch();
    void emitChanged();
    bool write(const std::string& key, const EventFile& file);
    void stamp(const std::string& key, Entry* entry) const;
    std::string fileNameFor(const std::string& calendarId, const std::string& uid) const;
    std::string pathOf(const std::string& key) const;

    std::string m_root;
    bool m_ready = false;
    // One change for a batch of them, as an import makes.
    bool m_batching = false;
    std::unordered_map<std::string, CalendarInfo> m_calendars;
    std::unordered_map<std::string, Entry> m_events;
    std::map<std::string, cutefish::base::FileWatch> m_watches;
    cutefish::base::Timer m_rescan;
    cutefish::base::Job m_scan;
};
