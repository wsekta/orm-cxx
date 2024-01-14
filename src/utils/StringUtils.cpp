#include "orm-cxx/utils/StringUtils.hpp"

namespace orm::utils
{
auto replaceAll(std::string& s, std::string const& toReplace, std::string const& replaceWith) -> void
{
    if (toReplace.empty())
    {
        return;
    }

    size_t start_pos = 0;

    while ((start_pos = s.find(toReplace, start_pos)) != std::string::npos)
    {
        s.replace(start_pos, toReplace.length(), replaceWith);
        start_pos += replaceWith.length();
    }
}
}