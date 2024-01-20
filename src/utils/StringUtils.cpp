#include "orm-cxx/utils/StringUtils.hpp"

namespace orm::utils
{
auto replaceAll(std::string& text, const std::string& toReplace, const std::string& replaceWith) -> void
{
    if (toReplace.empty())
    {
        return;
    }

    size_t start_pos = 0;

    while ((start_pos = text.find(toReplace, start_pos)) != std::string::npos)
    {
        text.replace(start_pos, toReplace.length(), replaceWith);
        start_pos += replaceWith.length();
    }
}
} // namespace orm::utils
