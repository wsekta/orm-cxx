#include "DefaultDeleteCommand.hpp"

#include <format>

namespace orm::db::commands
{
DefaultDeleteCommand::DefaultDeleteCommand(const SqlDialect& dialectInit) : dialect{dialectInit} {}

auto DefaultDeleteCommand::remove(const model::ModelInfo& modelInfo, const query::Predicate& predicate) const
    -> Statement
{
    RenderContext context{
        .modelInfo = modelInfo,
        .dialect = dialect,
        .shouldJoin = false,
        .columnRenderMode = ColumnRenderMode::WritePredicate,
    };

    const auto where = renderWhere(predicate, context);
    const auto sql = std::format("DELETE FROM {}{};", dialect.quoteIdentifier(modelInfo.tableName), where);

    return Statement{.sql = sql, .parameters = std::move(context.parameters)};
}
} // namespace orm::db::commands
