# The UI's logic, with no SDL, so it can all be tested directly.
add_library(modal_ui STATIC
    block_glyphs.cpp
)
add_library(modal::ui ALIAS modal_ui)

target_include_directories(modal_ui PUBLIC include)
target_link_libraries(modal_ui PRIVATE modal::warnings)

add_subdirectory(sdl)
