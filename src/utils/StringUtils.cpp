module;

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "soci/soci.h"

module orm;

import :internal;

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
