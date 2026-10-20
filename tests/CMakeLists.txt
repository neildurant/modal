find_package(Catch2 3 CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(SDL3 CONFIG REQUIRED) # to push events at windows
include(Catch)

add_executable(modal_tests
    core/count_in_test.cpp
    core/document_test.cpp
    core/engine_test.cpp
    core/event_test.cpp
    core/input_queue_test.cpp
    core/meter_map_test.cpp
    core/note_fraction_test.cpp
    core/pitch_test.cpp
    core/playback_plan_test.cpp
    core/recorder_test.cpp
    core/scheduler_test.cpp
    core/smf_export_test.cpp
    core/smf_test.cpp
    core/snapshot_exchange_test.cpp
    core/song_file_test.cpp
    core/song_test.cpp
    core/spsc_queue_test.cpp
    core/tempo_map_test.cpp
    core/thru_test.cpp
    core/timing_stats_test.cpp
    core/track_test.cpp
    core/version_test.cpp
    platform/clock_test.cpp
    platform/midi_io_test.cpp
    platform/midi_ports_test.cpp
    platform/paths_test.cpp
    platform/thread_priority_test.cpp
    ui/arrange_page_test.cpp
    ui/bar_lines_test.cpp
    ui/bar_test.cpp
    ui/block_glyphs_test.cpp
    ui/cell_grid_test.cpp
    ui/command_line_test.cpp
    ui/commands_test.cpp
    ui/config_test.cpp
    ui/demo_song_test.cpp
    ui/event_text_test.cpp
    ui/grid_cursor_test.cpp
    ui/key_notation_test.cpp
    ui/keymap_test.cpp
    ui/layout_test.cpp
    ui/prompt_test.cpp
    ui/settings_test.cpp
    ui/test_view_test.cpp
    ui/window_test.cpp
)

target_link_libraries(modal_tests PRIVATE
    modal::core modal::platform modal::ui modal::ui_sdl modal::warnings Catch2::Catch2WithMain Threads::Threads
    SDL3::SDL3)

# Tests read environment variables with std::getenv, which MSVC deprecates.
if(MSVC)
    target_compile_definitions(modal_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_include_directories(modal_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

catch_discover_tests(modal_tests)
