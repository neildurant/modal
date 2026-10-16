find_package(libremidi CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(modal_platform STATIC
    midi_input.cpp
    midi_output.cpp
    midi_ports.cpp
)
add_library(modal::platform ALIAS modal_platform)

if(WIN32)
    target_sources(modal_platform PRIVATE clock_win32.cpp console_win32.cpp thread_priority_win32.cpp)
    target_link_libraries(modal_platform PRIVATE avrt)
else()
    target_sources(modal_platform PRIVATE clock_linux.cpp console_linux.cpp thread_priority_linux.cpp)
endif()

target_include_directories(modal_platform PUBLIC include)
target_link_libraries(modal_platform
    PUBLIC modal::core Threads::Threads
    PRIVATE libremidi modal::warnings)
