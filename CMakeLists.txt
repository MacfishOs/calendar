cmake_minimum_required(VERSION 3.16)

project(cutefish-calendar VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CTest)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 DBus Concurrent)
find_package(CutefishFramework REQUIRED COMPONENTS Application)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBICAL REQUIRED IMPORTED_TARGET libical)

add_library(calendar-core STATIC
    src/calendarstore.cpp
    src/eventfile.cpp
    src/eventlayout.cpp
    src/reminders.cpp
    src/timezones.cpp
)
target_include_directories(calendar-core PUBLIC src)
target_link_libraries(calendar-core PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent PkgConfig::LIBICAL)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/application.cpp
    resources.qrc
)
target_compile_definitions(${PROJECT_NAME} PRIVATE CALENDAR_VERSION="${PROJECT_VERSION}")
target_link_libraries(${PROJECT_NAME} PRIVATE
    calendar-core
    Cutefish::Application
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
)

add_executable(cutefish-calendar-reminders
    src/remindersmain.cpp
    src/reminderservice.cpp
)
target_link_libraries(cutefish-calendar-reminders PRIVATE
    calendar-core
    Cutefish::Application
    Qt6::DBus
)

install(TARGETS ${PROJECT_NAME} cutefish-calendar-reminders RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES cutefish-calendar.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES cutefish-calendar-reminders.desktop DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
