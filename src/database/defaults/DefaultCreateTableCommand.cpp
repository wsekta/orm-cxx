#include "DefaultCreateTableCommand.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

#include "orm-cxx/utils/StringUtils.hpp"

namespace orm::db::commands
{
DefaultCreateTableCommand::DefaultCreateTableCommand(const SqlDialect& dialectInit) : dialect{dialectInit} {}

auto DefaultCreateTableCommand::createTable(model::ModelView model) const -> std::string
{
    std::string command = dialect.renderCreateTablePrefix(model->tableName, true) + "\n";

    for (const auto& column : model->columns)
    {
        if (column.kind == model::FieldKind::ToOne)
        {
            const auto target = model.resolveTarget(column);
            if (target == nullptr)
            {
                throw std::logic_error{"To-one relation target is not available in the schema"};
            }
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

    const auto hasToOne = std::ranges::any_of(model->columns, [](const model::ColumnView& column)
                                              { return column.kind == model::FieldKind::ToOne; });
    if (hasToOne)
    {
        command.append(std::format(",\n{}", addForeignIds(model)));
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

auto DefaultCreateTableCommand::addForeignIds(model::ModelView model) const -> std::string
{
    std::string command{};

    for (const auto& relationColumn : model->columns)
    {
        if (relationColumn.kind != model::FieldKind::ToOne)
        {
            continue;
        }

        const auto target = model.resolveTarget(relationColumn);
        if (target == nullptr)
        {
            throw std::logic_error{"To-one relation target is not available in the schema"};
        }

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
