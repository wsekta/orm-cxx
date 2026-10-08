#include "DefaultSelectCommand.hpp"
namespace orm::db::commands
{
DefaultSelectCommand::DefaultSelectCommand(const SqlDialect& dialectInit) : dialect{dialectInit} {}
auto DefaultSelectCommand::select(model::ModelView model,
                                  const query::detail::SelectSpec& spec) const -> SelectStatement
{
    return renderSelectStatement(model, spec, dialect);
}
}
