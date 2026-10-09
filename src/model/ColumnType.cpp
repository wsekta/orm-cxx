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

namespace orm::model
{
auto toString(ColumnType type) -> std::string
{
    switch (type)
    {
    case ColumnType::Bool:
        return "bool";
    case ColumnType::Char:
        return "char";
    case ColumnType::UnsignedChar:
        return "unsigned char";
    case ColumnType::Short:
        return "short";
    case ColumnType::UnsignedShort:
        return "unsigned short";
    case ColumnType::Int:
        return "int";
    case ColumnType::UnsignedInt:
        return "unsigned int";
    case ColumnType::LongLong:
        return "long long";
    case ColumnType::UnsignedLongLong:
        return "unsigned long long";
    case ColumnType::Float:
        return "float";
    case ColumnType::Double:
        return "double";
    case ColumnType::String:
        return "std::string";
    case ColumnType::Uuid:
        return "uuid";
    }

    return "unknown";
}
} // namespace orm::model
