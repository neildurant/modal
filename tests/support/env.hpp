// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace modal::test
{

// Whether an environment variable that enables machine-specific tests is set to 1.
inline bool envFlagSet(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && std::string_view{value} == "1";
}

#ifndef _WIN32
// An environment variable set for a test, and put back as it was afterwards, set or not.
class ScopedEnv
{
public:
    ScopedEnv(const char* name, const char* value)
        : name_(name)
    {
        if (const char* old = std::getenv(name))
        {
            old_ = old;
        }
        set(value);
    }

    ~ScopedEnv()
    {
        if (old_)
        {
            setenv(name_, old_->c_str(), 1);
        }
        else
        {
            unsetenv(name_);
        }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

    void set(const char* value) { setenv(name_, value, 1); }

private:
    const char* name_;
    std::optional<std::string> old_;
};
#endif

} // namespace modal::test
