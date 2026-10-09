module;

#include "soci/soci.h"
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

module orm;

import :internal;

namespace orm::db::commands
{
DefaultUpdateCommand::DefaultUpdateCommand(const SqlDialect& dialectInit) : dialect{dialectInit} {}
auto DefaultUpdateCommand::update(model::ModelView model, const query::detail::UpdateSpec& spec) const -> Statement
{
    return renderUpdateStatement(model, spec, dialect);
}
}
