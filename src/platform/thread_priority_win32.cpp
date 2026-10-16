#include <modal/platform/thread_priority.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off: windows.h must come first.
#include <windows.h>
#include <avrt.h>
// clang-format on

namespace modal::platform
{

RealtimePriority::RealtimePriority()
{
    DWORD taskIndex = 0;
    task_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (task_ == nullptr)
    {
        error_ = "can't join the MMCSS \"Pro Audio\" task (error " +
                 std::to_string(GetLastError()) + ")";
        return;
    }
    AvSetMmThreadPriority(task_, AVRT_PRIORITY_HIGH);
    raised_ = true;
}

RealtimePriority::~RealtimePriority()
{
    if (task_ != nullptr)
    {
        AvRevertMmThreadCharacteristics(task_);
    }
}

} // namespace modal::platform
