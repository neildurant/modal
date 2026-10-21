# The UI's logic, with no SDL, so it can all be tested directly.
add_library(modal_ui STATIC
    arrange_page.cpp
    bar.cpp
    bar_lines.cpp
    block_glyphs.cpp
    cell_grid.cpp
    command_line.cpp
    commands.cpp
    config.cpp
    demo_song.cpp
    event_list.cpp
    event_text.cpp
    inspector.cpp
    frame.cpp
    key_notation.cpp
    keymap.cpp
    grid_cursor.cpp
    layout.cpp
    prompt.cpp
    settings.cpp
    style.cpp
    event_edit.cpp
    event_list_view.cpp
    themes.cpp
    time_motions.cpp
    window_commands.cpp
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
