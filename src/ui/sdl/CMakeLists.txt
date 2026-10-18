find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)

add_library(modal_ui_sdl STATIC
    font.cpp
    window.cpp
)
modal_embed_file(TARGET modal_ui_sdl
    FILE "${PROJECT_SOURCE_DIR}/assets/fonts/JetBrainsMonoNL-Regular.ttf"
    FUNCTION modal::ui::sdl::builtInFont)
add_library(modal::ui_sdl ALIAS modal_ui_sdl)

target_include_directories(modal_ui_sdl PUBLIC include)
target_link_libraries(modal_ui_sdl PUBLIC modal::ui PRIVATE SDL3_ttf::SDL3_ttf SDL3::SDL3 modal::warnings)
