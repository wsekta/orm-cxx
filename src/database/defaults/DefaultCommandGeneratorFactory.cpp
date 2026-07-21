#include "DefaultCommandGeneratorFactory.hpp"

#include <utility>

#include "DefaultCreateTableCommand.hpp"
#include "DefaultDeleteCommand.hpp"
#include "DefaultDropTableCommand.hpp"
#include "DefaultInsertCommand.hpp"
#include "DefaultSelectCommand.hpp"
#include "DefaultUpdateCommand.hpp"
#include "orm-cxx/database/CommandGenerator.hpp"

namespace orm::db::defaults
{
auto makeDefaultCommandGenerator(const SqlDialect& dialect) -> std::unique_ptr<CommandGenerator>
{
    auto createTableCommand = std::make_unique<commands::DefaultCreateTableCommand>(dialect);
    auto dropTableCommand = std::make_unique<commands::DefaultDropTableCommand>(dialect);
    auto insertCommand = std::make_unique<commands::DefaultInsertCommand>(dialect);
    auto selectCommand = std::make_unique<commands::DefaultSelectCommand>(dialect);
    auto updateCommand = std::make_unique<commands::DefaultUpdateCommand>(dialect);
    auto deleteCommand = std::make_unique<commands::DefaultDeleteCommand>(dialect);

    return std::make_unique<CommandGenerator>(std::move(createTableCommand), std::move(dropTableCommand),
                                              std::move(insertCommand), std::move(selectCommand),
                                              std::move(updateCommand), std::move(deleteCommand));
}
} // namespace orm::db::defaults
