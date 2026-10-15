find_package(libremidi CONFIG REQUIRED)

add_library(modal_platform STATIC
    midi_ports.cpp
)
add_library(modal::platform ALIAS modal_platform)

target_include_directories(modal_platform PUBLIC include)
target_link_libraries(modal_platform PRIVATE libremidi modal::warnings)
