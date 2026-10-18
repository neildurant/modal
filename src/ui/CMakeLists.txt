# The UI's logic, with no SDL, so it can all be tested directly.
add_library(modal_ui STATIC
    arrange_page.cpp
    bar.cpp
    bar_lines.cpp
    block_glyphs.cpp
    cell_grid.cpp
    default_keymap.cpp
    demo_song.cpp
    frame.cpp
    key_notation.cpp
    keymap.cpp
    grid_cursor.cpp
    layout.cpp
    settings.cpp
    style.cpp
    test_view.cpp
)
add_library(modal::ui ALIAS modal_ui)

target_include_directories(modal_ui PUBLIC include)
target_link_libraries(modal_ui PUBLIC modal::core PRIVATE modal::warnings)

add_subdirectory(sdl)
