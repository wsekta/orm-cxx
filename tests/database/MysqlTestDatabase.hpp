#pragma once

#include "tests/database/postgresql/PostgresqlTestSchema.hpp"

namespace mysql_test
{
class MysqlTestDatabase
{
public:
    explicit MysqlTestDatabase(const orm::MysqlConfig& config)
        : name{postgresql_test::generateSchemaName()}, mysqlConnection{config}
    {
        mysqlConnection.database = name;
        create(config);
    }
    explicit MysqlTestDatabase(const orm::MariadbConfig& config)
        : name{postgresql_test::generateSchemaName()}, mariadbConnection{config}
    {
        mariadbConnection.database = name;
        create(config);
    }
    MysqlTestDatabase(const MysqlTestDatabase&) = delete;
    auto operator=(const MysqlTestDatabase&) -> MysqlTestDatabase& = delete;
    ~MysqlTestDatabase()
    {
        try
        {
            drop();
        }
        catch (...)
        {
        }
    }
    [[nodiscard]] auto mysqlConfig() const -> orm::MysqlConfig
    {
        return mysqlConnection;
    }
    [[nodiscard]] auto mariadbConfig() const -> orm::MariadbConfig
    {
        return mariadbConnection;
    }
    auto drop() -> void
    {
        if (not active)
            return;
        if (not postgresql_test::isSafeSchemaName(name))
            throw std::logic_error{"Unsafe test database cleanup"};
        adminSession << "DROP DATABASE `" + name + "`";
        active = false;
        adminSession.close();
    }

private:
    template <class Config>
    auto create(const Config& config) -> void
    {
        orm::db::CommandGeneratorFactory factory;
        factory.getBackend(orm::db::ConnectionTraits<Config>::backendType)
            .runtime()
            .open(adminSession, orm::db::ConnectionConfigView{config});
        adminSession << "CREATE DATABASE `" + name + "`";
        active = true;
    }
    std::string name;
    orm::MysqlConfig mysqlConnection;
    orm::MariadbConfig mariadbConnection;
    soci::session adminSession;
    bool active = false;
};
}
