find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(modal_tests
    core/version_test.cpp
)

target_link_libraries(modal_tests PRIVATE modal::core modal::warnings Catch2::Catch2WithMain)

catch_discover_tests(modal_tests)
