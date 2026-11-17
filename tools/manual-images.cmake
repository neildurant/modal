# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)
get_filename_component(modalRoot "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED MODAL_TESTS)
    message(FATAL_ERROR "Set MODAL_TESTS to a built modal_tests executable")
endif()
find_program(MAGICK_EXECUTABLE magick REQUIRED)
set(imageBuild "${modalRoot}/build/manual-images")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "MODAL_MANUAL_IMAGES=${imageBuild}" "${MODAL_TESTS}" "[.manual-images]"
    WORKING_DIRECTORY "${modalRoot}" COMMAND_ERROR_IS_FATAL ANY)
file(MAKE_DIRECTORY "${modalRoot}/docs/manual/images")
foreach(name arrangement piano-roll selection operator-hints palette arrangement-guide piano-roll-guide
        velocity-lane tracks metronome controller-ramp)
    execute_process(COMMAND "${MAGICK_EXECUTABLE}" "${imageBuild}/${name}.bmp"
        "${modalRoot}/docs/manual/images/${name}.png" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
