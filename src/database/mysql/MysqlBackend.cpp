module;

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

module orm;

import :internal;

namespace orm::db::mysql
{
MysqlBackend::MysqlBackend()
    : backendCapabilities{mysql_family::capabilities()},
      backendRuntime{mysql_family::makeRuntime(BackendType::Mysql, "mysql://")},
      mysqlCommandGenerator{defaults::makeDefaultCommandGenerator(mysqlDialect)}
{
}
MysqlBackend::~MysqlBackend() = default;
auto MysqlBackend::type() const noexcept -> BackendType
{
    return BackendType::Mysql;
}
auto MysqlBackend::acceptsConnectionString(std::string_view value) const noexcept -> bool
{
    return value.starts_with("mysql://");
}
auto MysqlBackend::capabilities() const noexcept -> const BackendCapabilities&
{
    return backendCapabilities;
}
auto MysqlBackend::dialect() const noexcept -> const SqlDialect&
{
    return mysqlDialect;
}
auto MysqlBackend::runtime() const noexcept -> const BackendRuntime&
{
    return *backendRuntime;
}
auto MysqlBackend::commandGenerator() const noexcept -> const CommandGenerator&
{
    return *mysqlCommandGenerator;
}
auto MysqlBackend::compiledSqlFlavor() const noexcept -> CompiledSqlFlavor
{
    return CompiledSqlFlavor::MySQLFamily;
}
}
