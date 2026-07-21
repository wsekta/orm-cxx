#pragma once

#include <memory>

namespace orm::db
{
class CommandGenerator;
class SqlDialect;
} // namespace orm::db

namespace orm::db::defaults
{
[[nodiscard]] auto makeDefaultCommandGenerator(const SqlDialect& dialect) -> std::unique_ptr<CommandGenerator>;
} // namespace orm::db::defaults
