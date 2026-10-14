#include <modal/core/version.hpp>

namespace modal::core
{

std::string_view version() noexcept
{
    return MODAL_VERSION;
}

} // namespace modal::core
