#pragma once

#include <cstdlib>
#include <string_view>

namespace modal::test
{

// Whether an environment variable that enables machine-specific tests is set to 1.
inline bool envFlagSet(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && std::string_view{value} == "1";
}

} // namespace modal::test
