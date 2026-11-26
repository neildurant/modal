add_library(modal_core STATIC
    drum_map.cpp
    drum_map_file.cpp
    text_format.cpp
    engine.cpp
    files.cpp
    freeze.cpp
    time_offset.cpp
    timing_stats.cpp
    event.cpp
    event_query.cpp
    input_queue.cpp
    meter_map.cpp
    note_fraction.cpp
    pedalling.cpp
    armed_range.cpp
    controllers.cpp
    count_in.cpp
    document.cpp
    chords.cpp
    pitch.cpp
    ramp.cpp
    playback_plan.cpp
    playback_transform.cpp
    player.cpp
    recorder.cpp
    recording_session.cpp
    scale.cpp
    scheduler.cpp
    smf.cpp
    smf_export.cpp
    song_file.cpp
    song.cpp
    takes.cpp
    tempo_map.cpp
    thru.cpp
    track.cpp
    transforms.cpp
    version.cpp
)
add_library(modal::core ALIAS modal_core)

target_include_directories(modal_core PUBLIC include)
target_compile_definitions(modal_core PRIVATE MODAL_VERSION="${PROJECT_VERSION}")
target_link_libraries(modal_core PRIVATE modal::warnings)
