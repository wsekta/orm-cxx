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

namespace orm::db
{
namespace
{
auto getInsertFields(const SqlDialect& dialect, const std::vector<std::string>& fieldNames) -> std::string
{
    std::string insertFields;

    for (const auto& fieldName : fieldNames)
    {
        insertFields += std::format("{}, ", dialect.quoteIdentifier(fieldName));
    }

    utils::removeLastComma(insertFields);

    return insertFields;
}

auto getInsertValues(const SqlDialect& dialect, const std::vector<std::string>& fieldNames) -> std::string
{
    std::string insertValues;

    for (const auto& fieldName : fieldNames)
    {
        insertValues += std::format("{}, ", dialect.bindMarker(fieldName));
    }

    utils::removeLastComma(insertValues);

    return insertValues;
}

auto getFieldsNames(model::ModelView model) -> std::vector<std::string>
{
    std::vector<std::string> fieldsNames;

    for (auto& binding : detail::insertColumnBindings(model))
    {
        fieldsNames.push_back(std::move(binding.logicalName));
    }

    return fieldsNames;
}
} // namespace

DefaultCommandGenerator::DefaultCommandGenerator(const SqlDialect& dialectInit, BackendType backendInit)
    : dialect{dialectInit}, backend{backendInit}
{
}

auto DefaultCommandGenerator::insert(model::ModelView model) const -> std::string
{
    auto fieldsNames = getFieldsNames(model);

    if (fieldsNames.empty())
    {
        return dialect.renderDefaultInsert(model->tableName);
    }

    return std::format("INSERT INTO {} ({}) VALUES ({});", dialect.quoteIdentifier(model->tableName),
                       getInsertFields(dialect, fieldsNames), getInsertValues(dialect, fieldsNames));
}

auto DefaultCommandGenerator::insertMany(model::ModelView model, const InsertManySpec& spec) const -> std::string
{
    if (spec.rowCount == 0)
    {
        throw std::invalid_argument{"A batch insert requires at least one row"};
    }

    const auto fieldsNames = getFieldsNames(model);

    if (fieldsNames.empty())
    {
        throw std::invalid_argument{"A batch insert requires at least one bound column"};
    }

    std::string sql = std::format("INSERT INTO {} ({}) VALUES ", dialect.quoteIdentifier(model->tableName),
                                  getInsertFields(dialect, fieldsNames));

    for (std::size_t row = 0; row < spec.rowCount; ++row)
    {
        if (row != 0)
        {
            sql += ", ";
        }

        sql += "(";

        for (std::size_t index = 0; index < fieldsNames.size(); ++index)
        {
            if (index != 0)
            {
                sql += ", ";
            }

            sql += dialect.bindMarker(insertManyParameterName(row, fieldsNames[index]));
        }

        sql += ")";
    }

    sql += ";";
    return sql;
}

auto DefaultCommandGenerator::createTable(model::ModelView model) const -> std::string
{
    const auto table = migrations::detail::physicalTable(model, dialect, backend);
    return migrations::detail::renderCreateTable(dialect, table, true, true);
}

auto DefaultCommandGenerator::dropTable(model::ModelView model) const -> std::string
{
    return dialect.renderDropTable(model->tableName, true);
}
auto DefaultCommandGenerator::select(model::ModelView model, const SelectSpec& spec) const -> SelectStatement
{
    return commands::renderSelectStatement(model, spec, dialect, backend);
}
auto DefaultCommandGenerator::update(model::ModelView model, const UpdateSpec& spec) const -> Statement
{
    return commands::renderUpdateStatement(model, spec, dialect, backend);
}
auto DefaultCommandGenerator::remove(model::ModelView model, const Predicate& predicate) const -> Statement
{
    return commands::renderRemoveStatement(model, predicate, dialect, backend);
}
} // namespace orm::db
