# The UI's logic, with no SDL, so it can all be tested directly.
add_library(modal_ui STATIC
    arrange_page.cpp
    arrange_view.cpp
    bar.cpp
    bar_lines.cpp
    block_glyphs.cpp
    cell_grid.cpp
    command_line.cpp
    commands.cpp
    completion.cpp
    palette.cpp
    ports.cpp
    ports_view.cpp
    metronome_view.cpp
    remembered.cpp
    config.cpp
    demo_song.cpp
    event_list.cpp
    event_text.cpp
    file_commands.cpp
    inspector.cpp
    inspector_edit.cpp
    musical_time.cpp
    frame.cpp
    key_notation.cpp
    keymap.cpp
    grid_cursor.cpp
    layout.cpp
    operators.cpp
    transform_specs.cpp
    prompt.cpp
    settings.cpp
    style.cpp
    event_edit.cpp
    event_list_view.cpp
    piano_roll_view.cpp
    themes.cpp
    time_motions.cpp
    track_fields.cpp
    tracks_view.cpp
    view_parts.cpp
    view_state.cpp
    window_commands.cpp
    workspace.cpp
    workspace_commands.cpp
    workspace_frame.cpp
    workspace_freeze.cpp
    workspace_hints.cpp
    workspace_macros.cpp
    workspace_prompt.cpp
    workspace_search.cpp
)
modal_embed_file(TARGET modal_ui
    FILE "${PROJECT_SOURCE_DIR}/assets/config/modalrc"
    FUNCTION modal::ui::builtInModalrcBytes)
modal_embed_file(TARGET modal_ui
    FILE "${PROJECT_SOURCE_DIR}/assets/themes/dark"
    FUNCTION modal::ui::builtInDarkTheme)
modal_embed_file(TARGET modal_ui
    FILE "${PROJECT_SOURCE_DIR}/assets/themes/light"
    FUNCTION modal::ui::builtInLightTheme)
add_library(modal::ui ALIAS modal_ui)

target_include_directories(modal_ui PUBLIC include)
target_link_libraries(modal_ui PUBLIC modal::core PRIVATE modal::warnings)

add_subdirectory(sdl)
