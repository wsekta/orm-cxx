#include "orm-cxx/utils/StringUtils.hpp"

namespace orm::utils
{
auto removeLastComma(std::string& text) -> void
{
    if (text.empty()) [[unlikely]]
    {
        return;
    }

    std::size_t pos = text.size() - 1;

    while ((pos > 0) and text[pos] != ',')
    {
        --pos;
    }

    if (text[pos] == ',')
    {
        text.resize(pos);
    }
}
} // namespace orm::utils
