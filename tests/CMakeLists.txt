find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(modal_tests
    core/note_fraction_test.cpp
    core/tempo_map_test.cpp
    core/version_test.cpp
    platform/midi_ports_test.cpp
)

target_link_libraries(modal_tests PRIVATE
    modal::core modal::platform modal::warnings Catch2::Catch2WithMain)

# Tests read environment variables with std::getenv, which MSVC deprecates.
if(MSVC)
    target_compile_definitions(modal_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_include_directories(modal_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

catch_discover_tests(modal_tests)
