module;

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

#include "soci/soci.h"

module orm;

import :internal;

namespace orm::db
{
CommandGenerator::CommandGenerator(std::unique_ptr<commands::CreateTableCommand> createTableCommandInit,
                                   std::unique_ptr<commands::DropTableCommand> dropTableCommandInit,
                                   std::unique_ptr<commands::InsertCommand> insertCommandInit,
                                   std::unique_ptr<commands::SelectCommand> selectCommandInit,
                                   std::unique_ptr<commands::UpdateCommand> updateCommandInit,
                                   std::unique_ptr<commands::DeleteCommand> deleteCommandInit)
    : createTableCommand(std::move(createTableCommandInit)),
      dropTableCommand(std::move(dropTableCommandInit)),
      insertCommand(std::move(insertCommandInit)),
      selectCommand(std::move(selectCommandInit)),
      updateCommand(std::move(updateCommandInit)),
      deleteCommand(std::move(deleteCommandInit))
{
}

auto CommandGenerator::createTable(model::ModelView model) const -> std::string
{
    return createTableCommand->createTable(model);
}

auto CommandGenerator::dropTable(model::ModelView model) const -> std::string
{
    return dropTableCommand->dropTable(model);
}

auto CommandGenerator::insert(model::ModelView model) const -> std::string
{
    return insertCommand->insert(model);
}

auto CommandGenerator::select(model::ModelView model, const query::detail::SelectSpec& spec) const -> SelectStatement
{
    return selectCommand->select(model, spec);
}

auto CommandGenerator::update(model::ModelView model, const query::detail::UpdateSpec& spec) const -> Statement
{
    return updateCommand->update(model, spec);
}

auto CommandGenerator::remove(model::ModelView model, const query::detail::Predicate& predicate) const -> Statement
{
    return deleteCommand->remove(model, predicate);
}
} // namespace orm::db
