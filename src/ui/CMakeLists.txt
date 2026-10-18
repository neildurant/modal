# The UI's logic, with no SDL, so it can all be tested directly.
add_library(modal_ui STATIC
    bar.cpp
    block_glyphs.cpp
    cell_grid.cpp
    frame.cpp
    layout.cpp
    style.cpp
)
add_library(modal::ui ALIAS modal_ui)

target_include_directories(modal_ui PUBLIC include)
target_link_libraries(modal_ui PRIVATE modal::warnings)

add_subdirectory(sdl)
