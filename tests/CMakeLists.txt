add_executable(tst_core
    tst_core.cpp
    ${PROJECT_SOURCE_DIR}/src/application.cpp
)
target_link_libraries(tst_core PRIVATE calendar-core Cutefish::Application Qt6::Test)
add_test(NAME calendar_core COMMAND tst_core)

add_executable(tst_ui
    tst_ui.cpp
    ${PROJECT_SOURCE_DIR}/src/application.cpp
    ${PROJECT_SOURCE_DIR}/resources.qrc
)
target_link_libraries(tst_ui PRIVATE calendar-core Cutefish::Application
    Qt6::Quick Qt6::QuickControls2 Qt6::Test)
add_test(NAME calendar_ui COMMAND tst_ui -platform offscreen)
