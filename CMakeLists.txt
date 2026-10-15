cmake_minimum_required(VERSION 3.25)

project(modal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings are errors on our own targets; pass --compile-no-warning-as-error to cmake to relax.
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

option(MODAL_BUILD_TESTS "Build the unit tests" ON)
set(MODAL_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. address,undefined (GCC/Clang only)")

include(cmake/ModalWarnings.cmake)

add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(apps)

if(MODAL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
