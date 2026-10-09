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
