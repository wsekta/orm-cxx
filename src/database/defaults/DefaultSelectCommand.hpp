#pragma once
#include "orm-cxx/database/commands/SelectCommand.hpp"
#include "orm-cxx/database/SqlDialect.hpp"
#include "SqlRenderer.hpp"
namespace orm::db::commands
{
class DefaultSelectCommand : public SelectCommand
{
public:
    explicit DefaultSelectCommand(const SqlDialect& dialectInit);
    [[nodiscard]] auto select(model::ModelView model,
                              const query::detail::SelectSpec& spec) const -> SelectStatement override;

private:
    const SqlDialect& dialect;
};
}
