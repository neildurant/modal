add_library(modal_core STATIC
    event.cpp
    meter_map.cpp
    note_fraction.cpp
    playback_plan.cpp
    scheduler.cpp
    smf.cpp
    song.cpp
    tempo_map.cpp
    track.cpp
    version.cpp
)
add_library(modal::core ALIAS modal_core)

target_include_directories(modal_core PUBLIC include)
target_compile_definitions(modal_core PRIVATE MODAL_VERSION="${PROJECT_VERSION}")
target_link_libraries(modal_core PRIVATE modal::warnings)
