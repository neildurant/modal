# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)
get_filename_component(modalRoot "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED MODAL_TESTS)
    message(FATAL_ERROR "Set MODAL_TESTS to a built modal_tests executable")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "MODAL_MANUAL_REFERENCES=${modalRoot}/docs/manual/reference"
    "${MODAL_TESTS}" "[manual-reference]"
    WORKING_DIRECTORY "${modalRoot}" COMMAND_ERROR_IS_FATAL ANY)
