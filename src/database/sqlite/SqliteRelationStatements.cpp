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

namespace
{
auto sqliteDialect() -> const orm::db::SqlDialect&
{
    static const orm::db::sqlite::SqliteDialect dialect;

    return dialect;
}
} // namespace

namespace orm::db::relations
{
auto createTableStatements(model::ModelView owner) -> std::vector<std::string>
{
    return createTableStatements(sqliteDialect(), owner);
}

auto dropTableStatements(model::ModelView owner) -> std::vector<std::string>
{
    return dropTableStatements(sqliteDialect(), owner);
}

auto linkStatement(model::ModelView owner, model::RelationView relation, const binding::PrimaryKey& ownerKey,
                   const binding::PrimaryKey& targetKey) -> Statement
{
    return linkStatement(sqliteDialect(), owner, relation, ownerKey, targetKey);
}

auto unlinkStatement(model::ModelView owner, model::RelationView relation, const binding::PrimaryKey& ownerKey,
                     const binding::PrimaryKey& targetKey) -> Statement
{
    return unlinkStatement(sqliteDialect(), owner, relation, ownerKey, targetKey);
}

auto collectionSelectStatement(model::ModelView owner, model::RelationView relation, std::string targetSelectSql,
                               const std::vector<binding::PrimaryKey>& ownerKeys, bool joinedValues) -> Statement
{
    return collectionSelectStatement(sqliteDialect(), owner, relation, std::move(targetSelectSql), ownerKeys,
                                     joinedValues);
}
} // namespace orm::db::relations
