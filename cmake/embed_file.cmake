# Run as a script by modal_embed_file (see EmbedFile.cmake): writes OUTPUT, a C++ source file
# defining FUNCTION as a function returning INPUT's bytes.

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" length)
math(EXPR size "${length} / 2")
# Sixteen bytes to a line, each written 0xNN.
string(REGEX REPLACE "([0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f])"
       "\\1\n" hex "${hex}")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")

string(REGEX MATCH "^(.*)::([^:]+)$" ignored "${FUNCTION}")
set(namespace "${CMAKE_MATCH_1}")
set(function "${CMAKE_MATCH_2}")

file(WRITE "${OUTPUT}" "// Generated from ${INPUT} by embed_file.cmake. Don't edit.

#include <span>

namespace ${namespace}
{

namespace
{

const unsigned char kBytes[${size}] = {
${bytes}
};

} // namespace

std::span<const unsigned char> ${function}()
{
    return kBytes;
}

} // namespace ${namespace}
")
