module;

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
      backendRuntime{mysql_family::makeRuntime(BackendType::Mariadb, "mariadb://")},
      mariadbCommandGenerator{defaults::makeDefaultCommandGenerator(mariadbDialect)}
{
    backendCapabilities.query.maxOrderedStringLength = 16'383;
}
MariadbBackend::~MariadbBackend() = default;
auto MariadbBackend::type() const noexcept -> BackendType
{
    return BackendType::Mariadb;
}
auto MariadbBackend::acceptsConnectionString(std::string_view value) const noexcept -> bool
{
    return value.starts_with("mariadb://");
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
    return *mariadbCommandGenerator;
}
auto MariadbBackend::compiledSqlFlavor() const noexcept -> CompiledSqlFlavor
{
    return CompiledSqlFlavor::MySQLFamily;
}
}
