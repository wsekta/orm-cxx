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

namespace orm::db::commands
{
DefaultCreateTableCommand::DefaultCreateTableCommand(const SqlDialect& dialectInit) : dialect{dialectInit} {}

auto DefaultCreateTableCommand::createTable(model::ModelView model) const -> std::string
{
    std::string command = dialect.renderCreateTablePrefix(model->tableName, true) + "\n";
    std::vector<ForeignKeyTarget> foreignKeys;

    for (const auto& column : model->columns)
    {
        if (column.kind == model::FieldKind::ToOne)
        {
            const auto target = model.resolveTarget(column);
            if (target == nullptr)
            {
                throw std::logic_error{"To-one relation target is not available in the schema"};
            }
            foreignKeys.push_back(ForeignKeyTarget{.relationColumn = column, .target = target});
            command.append(addColumnsForForeignIds(*target, column));

            continue;
        }

        if (column.isAutoIncrement)
        {
            command.append(std::format("\t{},\n", dialect.renderAutoIncrementPrimaryKey(column.name)));

            continue;
        }

        command.append(std::format("\t{} {}{},\n", dialect.quoteIdentifier(column.name),
                                   dialect.toSqlType(column.type.value()), column.isNotNull ? " NOT NULL" : ""));
    }

    if (model->primaryKeyIndices.empty() or hasAutoIncrementPrimaryKey(model))
    {
        utils::removeLastComma(command);
    }
    else
    {
        command.append("\tPRIMARY KEY (");

        for (const auto& column : model->columns)
        {
            if (column.isPrimaryKey)
            {
                command.append(std::format("{}, ", dialect.quoteIdentifier(column.name)));
            }
        }

        utils::removeLastComma(command);

        command.append(")");
    }

    if (not foreignKeys.empty())
    {
        command.append(std::format(",\n{}", addForeignIds(foreignKeys)));
    }

    command.append("\n);");

    return command;
}

auto DefaultCreateTableCommand::hasAutoIncrementPrimaryKey(model::ModelView model) -> bool
{
    for (const auto& column : model->columns)
    {
        if (column.isAutoIncrement)
        {
            return true;
        }
    }

    return false;
}

auto DefaultCreateTableCommand::addColumnsForForeignIds(model::ModelView target,
                                                        const model::ColumnView& column) const -> std::string
{
    std::string command{};

    const auto fieldName = column.name;
    const auto* const isNullable = column.isNotNull ? " NOT NULL" : "";

    for (const auto& targetColumn : target->columns)
    {
        if (targetColumn.isPrimaryKey)
        {
            command.append(std::format("\t{} {}{},\n",
                                       dialect.quoteIdentifier(std::format("{}_{}", fieldName, targetColumn.name)),
                                       dialect.toSqlType(targetColumn.type.value()), isNullable));
        }
    }

    return command;
}

auto DefaultCreateTableCommand::addForeignIds(const std::vector<ForeignKeyTarget>& foreignKeys) const -> std::string
{
    std::string command{};

    for (const auto& foreignKey : foreignKeys)
    {
        const auto& relationColumn = foreignKey.relationColumn;
        const auto target = foreignKey.target;

        std::string foreignKeyCommand{"\tFOREIGN KEY ("};
        for (const auto& targetColumn : target->columns)
        {
            if (targetColumn.isPrimaryKey)
            {
                foreignKeyCommand.append(std::format(
                    "{}, ", dialect.quoteIdentifier(std::format("{}_{}", relationColumn.name, targetColumn.name))));
            }
        }

        utils::removeLastComma(foreignKeyCommand);

        foreignKeyCommand.append(std::format(") REFERENCES {} (", dialect.quoteIdentifier(target->tableName)));

        for (const auto& targetColumn : target->columns)
        {
            if (targetColumn.isPrimaryKey)
            {
                foreignKeyCommand.append(std::format("{}, ", dialect.quoteIdentifier(targetColumn.name)));
            }
        }

        utils::removeLastComma(foreignKeyCommand);

        foreignKeyCommand.append("),\n");

        command.append(foreignKeyCommand);
    }

    if (not command.empty())
    {
        utils::removeLastComma(command);
    }

    return command;
}

} // namespace orm::db::commands
