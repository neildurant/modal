// SPDX-License-Identifier: GPL-3.0-or-later

// MSVC's debug runtime shows a dialog for a failed assertion, such as a vector's subscript out of
// range, which holds up the tests until someone answers it. Here they go to stderr instead, and
// the test fails, so nothing appears on the desktop of whoever is running them.

#ifdef _MSC_VER

#include <catch2/catch_test_macros.hpp>

#include <crtdbg.h>
#include <cstdlib>
#include <initializer_list>

namespace
{
[[maybe_unused]] const bool kNoDialogs = []
{
#ifdef _DEBUG // these do nothing in a release build
    for (const int kind : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
    {
        _CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
    }
#endif
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    return true;
}();
} // namespace

#ifdef _DEBUG
TEST_CASE("a failed assertion in a test opens no dialog")
{
    CHECK(_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_REPORT_MODE) == _CRTDBG_MODE_FILE);
    CHECK(_CrtSetReportMode(_CRT_ERROR, _CRTDBG_REPORT_MODE) == _CRTDBG_MODE_FILE);
}
#endif

#endif
