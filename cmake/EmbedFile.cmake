# Builds a file into a target as a byte array.
#
#   modal_embed_file(TARGET target FILE path FUNCTION qualified::name)
#
# defines std::span<const unsigned char> qualified::name() in a generated source file, which
# the target compiles. Declare the function in a header of your own. The file is read again
# whenever it changes.

function(modal_embed_file)
    cmake_parse_arguments(EMBED "" "TARGET;FILE;FUNCTION" "" ${ARGN})
    get_filename_component(name "${EMBED_FILE}" NAME)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/embedded/${name}.cpp")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${EMBED_FILE}" "-DOUTPUT=${output}"
                "-DFUNCTION=${EMBED_FUNCTION}" -P "${MODAL_EMBED_SCRIPT}"
        DEPENDS "${EMBED_FILE}" "${MODAL_EMBED_SCRIPT}"
        COMMENT "Embedding ${name}"
        VERBATIM)
    target_sources(${EMBED_TARGET} PRIVATE "${output}")
endfunction()

set(MODAL_EMBED_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/embed_file.cmake" CACHE INTERNAL "")
