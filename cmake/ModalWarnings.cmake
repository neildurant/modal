# Interface target carrying Modal's compiler warnings and sanitizer flags.
# Link it PRIVATE into every Modal target, never into third-party code.

add_library(modal_warnings INTERFACE)
add_library(modal::warnings ALIAS modal_warnings)

if(MSVC)
    target_compile_options(modal_warnings INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor)
else()
    target_compile_options(modal_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

if(MODAL_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "MODAL_SANITIZE is only supported with GCC and Clang")
    endif()
    target_compile_options(modal_warnings INTERFACE
        -fsanitize=${MODAL_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(modal_warnings INTERFACE -fsanitize=${MODAL_SANITIZE})
endif()
