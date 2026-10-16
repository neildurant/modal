find_package(Catch2 3 CONFIG REQUIRED)
find_package(Threads REQUIRED)
include(Catch)

add_executable(modal_tests
    core/event_test.cpp
    core/meter_map_test.cpp
    core/note_fraction_test.cpp
    core/playback_plan_test.cpp
    core/smf_test.cpp
    core/snapshot_exchange_test.cpp
    core/song_test.cpp
    core/spsc_queue_test.cpp
    core/tempo_map_test.cpp
    core/track_test.cpp
    core/version_test.cpp
    platform/midi_ports_test.cpp
)

target_link_libraries(modal_tests PRIVATE
    modal::core modal::platform modal::warnings Catch2::Catch2WithMain Threads::Threads)

# Tests read environment variables with std::getenv, which MSVC deprecates.
if(MSVC)
    target_compile_definitions(modal_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_include_directories(modal_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

catch_discover_tests(modal_tests)
