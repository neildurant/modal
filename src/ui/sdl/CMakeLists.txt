find_package(SDL3 CONFIG REQUIRED)

add_library(modal_ui_sdl STATIC
    window.cpp
)
add_library(modal::ui_sdl ALIAS modal_ui_sdl)

target_include_directories(modal_ui_sdl PUBLIC include)
target_link_libraries(modal_ui_sdl PRIVATE SDL3::SDL3 modal::warnings)
