cmake_minimum_required(VERSION 3.16)

project(cutefish-calendar VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CTest)

find_package(CutefishToolkit REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBICAL REQUIRED IMPORTED_TARGET libical)
pkg_check_modules(ICU REQUIRED IMPORTED_TARGET icu-uc icu-i18n)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

add_library(calendar-core STATIC
    src/calendarmodel.cpp
    src/calendarstore.cpp
    src/color.cpp
    src/datetime.cpp
    src/download.cpp
    src/eventfile.cpp
    src/eventlayout.cpp
    src/holidayfeeds.cpp
    src/localedata.cpp
    src/reminders.cpp
    src/sha1.cpp
    src/subscriptions.cpp
    src/text.cpp
    src/timezones.cpp
    src/trash.cpp
    src/userdirs.cpp
    src/uuid.cpp
)
target_include_directories(calendar-core PUBLIC src)
target_link_libraries(calendar-core PUBLIC Cutefish::ToolkitBase PkgConfig::LIBICAL PkgConfig::ICU PkgConfig::CURL)

add_library(calendar-ui STATIC
    src/ui/calendarroot.cpp
    src/ui/calendarwindow.cpp
    src/ui/colors.cpp
    src/ui/dialogs.cpp
    src/ui/eventdialog.cpp
    src/ui/eventviews.cpp
    src/ui/glyphs.cpp
    src/ui/headerbar.cpp
    src/ui/minimonth.cpp
    src/ui/monthview.cpp
    src/ui/searchresults.cpp
    src/ui/sidebar.cpp
    src/ui/timegridview.cpp
    src/ui/yearview.cpp
)
target_link_libraries(calendar-ui PUBLIC calendar-core Cutefish::Toolkit)

add_executable(cutefish-calendar src/main.cpp)
target_compile_definitions(cutefish-calendar PRIVATE CALENDAR_VERSION="${PROJECT_VERSION}")
target_link_libraries(cutefish-calendar PRIVATE calendar-ui)

add_executable(cutefish-calendar-reminders
    src/remindersmain.cpp
    src/reminderservice.cpp
)
target_link_libraries(cutefish-calendar-reminders PRIVATE calendar-core)

install(TARGETS cutefish-calendar cutefish-calendar-reminders RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

cutefish_add_translations(cutefish-calendar SOURCES src
                          DESKTOP_FILES cutefish-calendar.desktop cutefish-calendar-reminders.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cutefish-calendar.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cutefish-calendar-reminders.desktop
        DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()
