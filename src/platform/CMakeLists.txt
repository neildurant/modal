find_package(libremidi CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(modal_platform STATIC
    midi_input.cpp
    midi_input_session.cpp
    midi_output.cpp
    midi_ports.cpp
)
add_library(modal::platform ALIAS modal_platform)

if(WIN32)
    target_sources(modal_platform PRIVATE arguments_win32.cpp clock_win32.cpp console_win32.cpp paths_win32.cpp thread_priority_win32.cpp)
    target_link_libraries(modal_platform PRIVATE avrt ole32 shell32)
else()
    target_sources(modal_platform PRIVATE arguments_linux.cpp clock_linux.cpp console_linux.cpp paths_linux.cpp thread_priority_linux.cpp)
endif()

target_include_directories(modal_platform PUBLIC include)
target_link_libraries(modal_platform
    PUBLIC modal::core Threads::Threads
    PRIVATE libremidi modal::warnings)
