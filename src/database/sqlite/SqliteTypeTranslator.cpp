module;

#include "soci/soci.h"
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
#include <regex>
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

module orm;

import :internal;

namespace
{
const std::regex stringRegex{R"((class )?std.*::basic_string<.*>\W?)"};
}

namespace orm::db::sqlite
{
auto SqliteTypeTranslator::toSqlType(model::ColumnType type) const -> std::string
{
    switch (type)
    {
    case model::ColumnType::Bool:
        return "BOOLEAN";

    case model::ColumnType::Char:
    case model::ColumnType::UnsignedChar:
        return "TINYINT";

    case model::ColumnType::Short:
    case model::ColumnType::UnsignedShort:
        return "SMALLINT";

    case model::ColumnType::Int:
        return "INTEGER";

    case model::ColumnType::LongLong:
        return "BIGINT";

    case model::ColumnType::UnsignedInt:
    case model::ColumnType::UnsignedLongLong:
        return "UNSIGNED BIG INT";

    case model::ColumnType::Float:
    case model::ColumnType::Double:
        return "REAL";

    case model::ColumnType::String:
        return "TEXT";

    default:
        throw std::runtime_error("Unsupported type by sqlite " + orm::model::toString(type));
    }
}
} // namespace orm::db::sqlite
