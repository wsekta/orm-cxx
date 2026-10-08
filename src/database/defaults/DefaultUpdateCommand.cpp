#include "DefaultUpdateCommand.hpp"
namespace orm::db::commands
{
DefaultUpdateCommand::DefaultUpdateCommand(const SqlDialect& dialectInit) : dialect{dialectInit} {}
auto DefaultUpdateCommand::update(model::ModelView model, const query::detail::UpdateSpec& spec) const -> Statement
{
    return renderUpdateStatement(model, spec, dialect);
}
}
