#include "DefaultDeleteCommand.hpp"
namespace orm::db::commands
{
DefaultDeleteCommand::DefaultDeleteCommand(const SqlDialect& dialectInit) : dialect{dialectInit} {}
auto DefaultDeleteCommand::remove(model::ModelView model, const query::detail::Predicate& predicate) const -> Statement
{
    return renderRemoveStatement(model, predicate, dialect);
}
}
