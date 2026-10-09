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

namespace orm::db::postgresql
{
auto PostgresqlTypeTranslator::toSqlType(model::ColumnType type) const -> std::string
{
    switch (type)
    {
    case model::ColumnType::Bool:
        return "BOOLEAN";

    case model::ColumnType::Char:
    case model::ColumnType::UnsignedChar:
    case model::ColumnType::Short:
        return "SMALLINT";

    case model::ColumnType::UnsignedShort:
    case model::ColumnType::Int:
        return "INTEGER";

    case model::ColumnType::UnsignedInt:
    case model::ColumnType::LongLong:
    case model::ColumnType::UnsignedLongLong:
        return "BIGINT";

    case model::ColumnType::Float:
    case model::ColumnType::Double:
        return "DOUBLE PRECISION";

    case model::ColumnType::String:
        return "TEXT";

    case model::ColumnType::Uuid:
        break;
    }

    throw std::runtime_error{"Unsupported type by PostgreSQL " + orm::model::toString(type)};
}
} // namespace orm::db::postgresql
