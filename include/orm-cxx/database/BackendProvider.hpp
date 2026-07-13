#pragma once

#include <string_view>

#include "BackendCapabilities.hpp"
#include "BackendType.hpp"

namespace orm::db
{
class BackendRuntime;
class CommandGenerator;
class SqlDialect;

class BackendProvider
{
public:
    virtual ~BackendProvider() = default;

    [[nodiscard]] virtual auto type() const noexcept -> BackendType = 0;
    [[nodiscard]] virtual auto acceptsConnectionString(std::string_view connectionString) const noexcept -> bool = 0;
    [[nodiscard]] virtual auto capabilities() const noexcept -> const BackendCapabilities& = 0;
    [[nodiscard]] virtual auto dialect() const noexcept -> const SqlDialect& = 0;
    [[nodiscard]] virtual auto runtime() const noexcept -> const BackendRuntime& = 0;
    [[nodiscard]] virtual auto commandGenerator() const noexcept -> const CommandGenerator& = 0;
};
} // namespace orm::db
