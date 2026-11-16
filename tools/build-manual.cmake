# SPDX-License-Identifier: GPL-3.0-or-later

# Stage shared fonts without keeping another copy in the manual's sources. No application
# build, package installation or network access is needed to publish the checked-in book.
cmake_minimum_required(VERSION 3.25)
get_filename_component(modalRoot "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(manualSource "${modalRoot}/docs/manual")
set(manualStage "${modalRoot}/build/manual-source")
set(manualOutput "${modalRoot}/build/manual")
find_program(QUARTO_EXECUTABLE quarto REQUIRED)
execute_process(COMMAND "${QUARTO_EXECUTABLE}" --version
    OUTPUT_VARIABLE quartoVersion OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT quartoVersion STREQUAL "1.11.9")
    message(FATAL_ERROR "Use Quarto 1.11.9 for this manual; found ${quartoVersion}")
endif()

# Remove the staging copy so deleted chapters or styles cannot survive a rebuild.
file(REMOVE_RECURSE "${manualStage}")
file(MAKE_DIRECTORY "${manualStage}")
file(COPY "${manualSource}/" DESTINATION "${manualStage}"
    PATTERN "_book" EXCLUDE PATTERN ".quarto" EXCLUDE PATTERN "README.md" EXCLUDE)
file(COPY
    "${modalRoot}/assets/fonts/JetBrainsMonoNL-Regular.ttf"
    "${modalRoot}/assets/fonts/JetBrainsMonoNL-Bold.ttf"
    "${modalRoot}/assets/fonts/OFL.txt"
    DESTINATION "${manualStage}/styles/fonts")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "XDG_CACHE_HOME=${modalRoot}/build/manual-cache"
    "${QUARTO_EXECUTABLE}" render . --to all
    WORKING_DIRECTORY "${manualStage}" COMMAND_ERROR_IS_FATAL ANY)
# Replace only this fixed generated directory, after a successful render. Old chapters and
# hashed styles must not survive publication; a failed render leaves the last output intact.
file(REMOVE_RECURSE "${manualOutput}")
file(MAKE_DIRECTORY "${manualOutput}")
file(COPY "${manualStage}/_book/" DESTINATION "${manualOutput}")
message(STATUS "Manual written to ${manualOutput}")
