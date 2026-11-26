# The UI's logic, with no SDL, so it can all be tested directly.
add_library(modal_ui STATIC
    arrange_page.cpp
    arrange_view.cpp
    bar.cpp
    bar_lines.cpp
    block_glyphs.cpp
    cell_grid.cpp
    choice_list.cpp
    command_line.cpp
    commands.cpp
    completion.cpp
    palette.cpp
    ports.cpp
    ports_view.cpp
    metronome_view.cpp
    remembered.cpp
    backups.cpp
    base16.cpp
    clipboard.cpp
    workspace_backups.cpp
    workspace_jumps.cpp
    workspace_paste.cpp
    workspace_punch.cpp
    workspace_takes.cpp
    config.cpp
    drum_maps.cpp
    workspace_drum_maps.cpp
    workspace_event_insert.cpp
    workspace_file_picker.cpp
    workspace_track_import.cpp
    workspace_recent.cpp
    recent_songs.cpp
    track_import.cpp
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
    status_bar.cpp
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
    workspace_loop.cpp
    workspace_macros.cpp
    workspace_prompt.cpp
    workspace_search.cpp
)
modal_embed_file(TARGET modal_ui
    FILE "${PROJECT_SOURCE_DIR}/assets/config/modalrc"
    FUNCTION modal::ui::builtInModalrcBytes)
modal_embed_file(TARGET modal_ui
    FILE "${PROJECT_SOURCE_DIR}/assets/themes/darker"
    FUNCTION modal::ui::builtInDarkerTheme)
modal_embed_file(TARGET modal_ui
    FILE "${PROJECT_SOURCE_DIR}/assets/themes/dark"
    FUNCTION modal::ui::builtInDarkTheme)
modal_embed_file(TARGET modal_ui
    FILE "${PROJECT_SOURCE_DIR}/assets/themes/light"
    FUNCTION modal::ui::builtInLightTheme)
# The base16 schemes built in (decided 24 November 2026), in the order themes.cpp names them.
set(MODAL_BASE16_SCHEMES
    gruvbox-dark-medium gruvbox-light-medium nord dracula solarized-dark solarized-light
    catppuccin-mocha catppuccin-latte)
set(scheme_index 0)
foreach(scheme IN LISTS MODAL_BASE16_SCHEMES)
    modal_embed_file(TARGET modal_ui
        FILE "${PROJECT_SOURCE_DIR}/assets/themes/base16/${scheme}.yaml"
        FUNCTION modal::ui::builtInScheme${scheme_index})
    math(EXPR scheme_index "${scheme_index} + 1")
endforeach()
add_library(modal::ui ALIAS modal_ui)

target_include_directories(modal_ui PUBLIC include)
target_link_libraries(modal_ui PUBLIC modal::core PRIVATE modal::warnings)

add_subdirectory(sdl)
