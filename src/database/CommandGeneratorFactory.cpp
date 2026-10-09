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
namespace
{
// Disabled providers remain incomplete. Instantiate their registration only when
// the configured backend is enabled, so its vtable never enters a core-only build.
template <bool Enabled, typename Backend>
auto registerBuiltinBackend(CommandGeneratorFactory& factory) -> void
{
    if constexpr (Enabled)
    {
        factory.registerBackend(std::make_unique<Backend>());
    }
}
}

CommandGeneratorFactory::CommandGeneratorFactory()
{
    registerBuiltinBackend<config::sqliteBackendEnabled, sqlite::SqliteBackend>(*this);
    registerBuiltinBackend<config::postgresqlBackendEnabled, postgresql::PostgresqlBackend>(*this);
}

auto CommandGeneratorFactory::registerBackend(std::unique_ptr<BackendProvider> backend) -> void
{
    if (backend == nullptr)
    {
        throw std::invalid_argument{"Cannot register a null backend"};
    }

    const auto backendType = backend->type();

    if (backendType == BackendType::Empty)
    {
        throw std::invalid_argument{"BackendType::Empty is reserved for disconnected databases"};
    }

    if (backends.contains(backendType))
    {
        throw std::invalid_argument{"Backend is already registered"};
    }

    backends.emplace(backendType, std::move(backend));
}

auto CommandGeneratorFactory::getBackend(BackendType backendType) const -> const BackendProvider&
{
    return *backends.at(backendType);
}

auto CommandGeneratorFactory::findBackend(std::string_view connectionString) const noexcept -> const BackendProvider*
{
    const BackendProvider* match = nullptr;

    for (const auto& entry : backends)
    {
        const auto& backend = entry.second;

        if (backend->acceptsConnectionString(connectionString))
        {
            if (match != nullptr)
            {
                // Predicate-based discovery must never depend on the iteration
                // order of the registry. Treat overlapping providers as an
                // ambiguous, unsupported connection string.
                return nullptr;
            }

            match = backend.get();
        }
    }

    return match;
}

auto CommandGeneratorFactory::getCommandGenerator(BackendType backendType) const -> const CommandGenerator&
{
    return getBackend(backendType).commandGenerator();
}
} // namespace orm::db
