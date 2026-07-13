#pragma once

#include <memory>

#include "orm-cxx/database/BackendProvider.hpp"
#include "SqliteDialect.hpp"

namespace orm::db::sqlite
{
class SqliteBackend final : public BackendProvider
{
public:
    SqliteBackend();
    ~SqliteBackend() override;

    [[nodiscard]] auto type() const noexcept -> BackendType override;
    [[nodiscard]] auto acceptsConnectionString(std::string_view connectionString) const noexcept -> bool override;
    [[nodiscard]] auto capabilities() const noexcept -> const BackendCapabilities& override;
    [[nodiscard]] auto dialect() const noexcept -> const SqlDialect& override;
    [[nodiscard]] auto runtime() const noexcept -> const BackendRuntime& override;
    [[nodiscard]] auto commandGenerator() const noexcept -> const CommandGenerator& override;

private:
    BackendCapabilities backendCapabilities;
    SqliteDialect sqliteDialect;
    std::unique_ptr<BackendRuntime> backendRuntime;
    std::unique_ptr<CommandGenerator> sqliteCommandGenerator;
};
} // namespace orm::db::sqlite
