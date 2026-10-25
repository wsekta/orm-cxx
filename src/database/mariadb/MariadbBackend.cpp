module;

#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

module orm;

import :internal;

namespace orm::db::mariadb
{
MariadbBackend::MariadbBackend()
    : backendCapabilities{mysql_family::capabilities()},
      backendRuntime{mysql_family::makeRuntime(BackendType::Mariadb)},
      mariadbCommandGenerator{mariadbDialect}
{
    backendCapabilities.query.maxOrderedStringLength = 16'383;
    backendCapabilities.supportedColumnTypes.push_back(model::ColumnType::Uuid);
    backendCapabilities.valueLimits.maxLocalDateTime =
        *backendCapabilities.valueLimits.maxLocalDateTime + std::chrono::microseconds{500000};
    backendCapabilities.valueLimits.maxUtcTimestamp =
        orm::UtcTimestamp{std::chrono::seconds{4'294'967'295}} + std::chrono::microseconds{999999};
}
MariadbBackend::~MariadbBackend() = default;
auto MariadbBackend::type() const noexcept -> BackendType
{
    return BackendType::Mariadb;
}
auto MariadbBackend::capabilities() const noexcept -> const BackendCapabilities&
{
    return backendCapabilities;
}
auto MariadbBackend::dialect() const noexcept -> const SqlDialect&
{
    return mariadbDialect;
}
auto MariadbBackend::runtime() const noexcept -> const BackendRuntime&
{
    return *backendRuntime;
}
auto MariadbBackend::commandGenerator() const noexcept -> const CommandGenerator&
{
    return mariadbCommandGenerator;
}
auto MariadbBackend::compiledSqlFlavor() const noexcept -> CompiledSqlFlavor
{
    return CompiledSqlFlavor::MySQLFamily;
}
}
