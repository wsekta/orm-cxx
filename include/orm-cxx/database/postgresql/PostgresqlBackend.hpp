#pragma once

#include <memory>

#include "orm-cxx/database/BackendProvider.hpp"
#include "PostgresqlDialect.hpp"

namespace orm::db::postgresql
{
class PostgresqlBackend final : public BackendProvider
{
public:
    PostgresqlBackend();
    ~PostgresqlBackend() override;

    [[nodiscard]] auto type() const noexcept -> BackendType override;
    [[nodiscard]] auto acceptsConnectionString(std::string_view connectionString) const noexcept -> bool override;
    [[nodiscard]] auto capabilities() const noexcept -> const BackendCapabilities& override;
    [[nodiscard]] auto dialect() const noexcept -> const SqlDialect& override;
    [[nodiscard]] auto runtime() const noexcept -> const BackendRuntime& override;
    [[nodiscard]] auto commandGenerator() const noexcept -> const CommandGenerator& override;

private:
    BackendCapabilities backendCapabilities;
    PostgresqlDialect postgresqlDialect;
    std::unique_ptr<BackendRuntime> backendRuntime;
    std::unique_ptr<CommandGenerator> postgresqlCommandGenerator;
};
} // namespace orm::db::postgresql
