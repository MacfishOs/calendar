# Calendar

The calendar of Cutefish Desktop. It shows events by day, week, month and
year, with repeating events, alerts and several coloured calendars, and reads
and writes standard iCalendar files.

Events are kept as one `.ics` file per event in
`~/.local/share/cutefish-calendar/calendars/`, one directory per calendar.
`cutefish-calendar-reminders` starts with the session and shows alerts as
notifications while the calendar is closed.

## Dependencies

Calendar uses the Cutefish toolkit (`cutefish-toolkit`), libical and ICU.

```sh
sudo apt install cmake pkg-config libical-dev libicu-dev libgtest-dev
```

## Build and install

```sh
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
make
sudo make install
```

## Tests

```sh
cmake -DBUILD_TESTING=ON ..
make
dbus-run-session -- ctest --output-on-failure
```

## License

This project has been licensed by GPLv3.
