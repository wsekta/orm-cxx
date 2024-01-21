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

auto removeLastComma(std::string& text) -> void
{
    std::size_t pos = text.size();

    while (--pos and text[pos] != ',')
    {
    }

    if (text[pos] == ',')
    {
        text = text.substr(0, pos);
    }
}
} // namespace orm::utils
