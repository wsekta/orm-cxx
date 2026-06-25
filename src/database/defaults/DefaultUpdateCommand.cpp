#include "DefaultUpdateCommand.hpp"

#include <format>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
auto join(const std::vector<std::string>& parts, std::string_view separator) -> std::string
{
    std::string joined;

    for (const auto& part : parts)
    {
        joined += part;
        joined += separator;
    }

    if (not joined.empty())
    {
        joined.resize(joined.size() - separator.size());
    }

    return joined;
}
} // namespace

namespace orm::db::commands
{
auto DefaultUpdateCommand::update(const query::UpdateData& updateData) const -> Statement
{
    if (updateData.assignments.empty())
    {
        throw std::invalid_argument{"UPDATE requires at least one assignment"};
    }

    if (not updateData.predicate.has_value())
    {
        throw std::invalid_argument{"UPDATE requires a WHERE predicate"};
    }

    RenderContext context{
        .modelInfo = updateData.modelInfo,
        .shouldJoin = false,
        .columnRenderMode = ColumnRenderMode::WritePredicate,
    };

    const auto assignments = getAssignments(updateData, context);
    const auto where = renderWhere(updateData.predicate, context);
    const auto sql = std::format("UPDATE {} SET {}{};", updateData.modelInfo.tableName, assignments, where);

    return Statement{.sql = sql, .parameters = std::move(context.parameters)};
}

auto DefaultUpdateCommand::getAssignments(const query::UpdateData& updateData, RenderContext& context) -> std::string
{
    std::vector<std::string> assignments;
    assignments.reserve(updateData.assignments.size());

    for (const auto& assignment : updateData.assignments)
    {
        const auto column = renderWriteColumn(assignment.column, updateData.modelInfo, false);
        std::string parameter;

        if (assignment.value.value.has_value())
        {
            parameter = addAutomaticParameter(context, assignment.value.value.value());
        }
        else
        {
            if (column.isNotNull)
            {
                throw std::invalid_argument{"Cannot assign NULL to NOT NULL column: " + assignment.column.getPath()};
            }

            parameter = addNullParameter(context, column.type);
        }

        assignments.push_back(std::format("{} = {}", column.sql, parameter));
    }

    return join(assignments, ", ");
}
} // namespace orm::db::commands
