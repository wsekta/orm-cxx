#pragma once

#include <cstddef>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "orm-cxx-tests/TestConfig.hpp"
#include "tests/database/MysqlTestDatabase.hpp"
#include "tests/database/postgresql/PostgresqlTestSchema.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/support/ConnectionConfig.hpp"
#include "tests/utils/GenerateModels.hpp"

struct BackendTestConfig
{
    orm::db::BackendType type;
    std::string name;
    bool supported;
};

namespace orm::test::fixtures
{
inline constexpr std::size_t modelCount = 10;

extern const auto sqliteBackendTestConfig = BackendTestConfig{
    .type = orm::db::BackendType::Sqlite,
    .name = "Sqlite",
    .supported = true,
};

extern const auto postgresqlBackendTestConfig = BackendTestConfig{
    .type = orm::db::BackendType::Postgres,
    .name = "Postgresql",
    .supported = true,
};

[[maybe_unused]] extern const auto backendTestConfigs = ::testing::ValuesIn(
    []
    {
        std::vector<BackendTestConfig> configs;
        if constexpr (orm::config::sqliteBackendEnabled)
            configs.push_back(sqliteBackendTestConfig);
        if constexpr (orm::test::config::postgresqlIntegrationEnabled)
        {
            configs.push_back(postgresqlBackendTestConfig);
        }
        if constexpr (orm::test::config::mysqlIntegrationEnabled)
            configs.push_back({orm::db::BackendType::Mysql, "Mysql", true});
        if constexpr (orm::test::config::mariadbIntegrationEnabled)
            configs.push_back({orm::db::BackendType::Mariadb, "Mariadb", true});
        return configs;
    }());
[[maybe_unused]] extern const auto conformanceBackendTestConfigs = backendTestConfigs;

auto backendTestName(const ::testing::TestParamInfo<BackendTestConfig>& info) -> std::string
{
    return info.param.name;
}
} // namespace

template <typename SchemaType = models::Schema>
class DatabaseTest : public ::testing::TestWithParam<BackendTestConfig>
{
private:
    using TestConnectionConfig =
        std::variant<orm::SqliteConfig, orm::PostgresConfig, orm::MysqlConfig, orm::MariadbConfig>;
    std::vector<std::function<void()>> tearDownFunctions;
    std::unique_ptr<postgresql_test::PostgresqlTestSchema> postgresqlSchema;
    std::unique_ptr<mysql_test::MysqlTestDatabase> mysqlDatabase;
    TestConnectionConfig activeConfig = initializeTestConfiguration();

    auto initializeTestConfiguration() -> TestConnectionConfig
    {
        switch (this->GetParam().type)
        {
        case orm::db::BackendType::Sqlite:
            return orm::SqliteConfig{.database = orm::SqliteMemory{}};
        case orm::db::BackendType::Postgres:
            postgresqlSchema =
                std::make_unique<postgresql_test::PostgresqlTestSchema>(orm::test::postgresConfigFromEnvironment());
            return postgresqlSchema->connectionConfig();
        case orm::db::BackendType::Mysql:
            mysqlDatabase = std::make_unique<mysql_test::MysqlTestDatabase>(orm::test::mysqlConfigFromEnvironment());
            return mysqlDatabase->mysqlConfig();
        case orm::db::BackendType::Mariadb:
            mysqlDatabase = std::make_unique<mysql_test::MysqlTestDatabase>(orm::test::mariadbConfigFromEnvironment());
            return mysqlDatabase->mariadbConfig();
        default:
            throw std::logic_error{"Unsupported test backend"};
        }
    }

public:
    [[nodiscard]] auto makeTestDatabase() -> orm::Database
    {
        switch (this->GetParam().type)
        {
        case orm::db::BackendType::Sqlite:
            return orm::Database{std::get<orm::SqliteConfig>(activeConfig)};
        case orm::db::BackendType::Postgres:
            return orm::Database{std::get<orm::PostgresConfig>(activeConfig), postgresqlSchema->registry()};
        case orm::db::BackendType::Mysql:
            return orm::Database{std::get<orm::MysqlConfig>(activeConfig)};
        case orm::db::BackendType::Mariadb:
            return orm::Database{std::get<orm::MariadbConfig>(activeConfig)};
        default:
            throw std::logic_error{"Unsupported test backend"};
        }
    }

    auto connectTestDatabase(orm::Database& owner) const -> void
    {
        std::visit([&owner](const auto& config) { owner.connect(config); }, activeConfig);
    }

    [[nodiscard]] auto testConnectionConfig() const noexcept -> const TestConnectionConfig&
    {
        return activeConfig;
    }

    orm::Database connection = makeTestDatabase();
    orm::OrmContext<SchemaType> database = connection.orm<SchemaType>();

    auto SetUp() -> void override {}

    auto TearDown() -> void override
    {
        for (auto tearDownFunction = tearDownFunctions.rbegin(); tearDownFunction != tearDownFunctions.rend();
             ++tearDownFunction)
        {
            try
            {
                (*tearDownFunction)();
            }
            catch (const std::exception& error)
            {
                ADD_FAILURE() << "Database test cleanup failed: " << error.what();
            }
            catch (...)
            {
                ADD_FAILURE() << "Database test cleanup failed with an unknown error";
            }
        }

        if (connection.isConnected())
        {
            try
            {
                connection.disconnect();
            }
            catch (const std::exception& error)
            {
                ADD_FAILURE() << "Database disconnect during cleanup failed: " << error.what();
            }
            catch (...)
            {
                ADD_FAILURE() << "Database disconnect during cleanup failed with an unknown error";
            }
        }

        if (mysqlDatabase != nullptr)
        {
            try
            {
                mysqlDatabase->drop();
            }
            catch (const std::exception& error)
            {
                ADD_FAILURE() << "Test database cleanup failed: " << error.what();
            }
            mysqlDatabase.reset();
        }
        if constexpr (orm::test::config::postgresqlIntegrationEnabled)
        {
            if (postgresqlSchema != nullptr)
            {
                try
                {
                    postgresqlSchema->drop();
                }
                catch (const std::exception& error)
                {
                    ADD_FAILURE() << "Failed to drop isolated PostgreSQL test schema: " << error.what();
                }
                postgresqlSchema.reset();
            }
        }
    }

    template <typename T>
    auto createTable() -> void
    {
        database.template createTable<T>();
        tearDownFunctions.emplace_back([this]() { database.template deleteTable<T>(); });
    }

    template <typename T>
    auto createRelationTables() -> void
    {
        database.template createRelationTables<T>();
        tearDownFunctions.emplace_back([this]() { database.template deleteRelationTables<T>(); });
    }

    auto postgresqlTestSchema() noexcept -> postgresql_test::PostgresqlTestSchema*
    {
        return postgresqlSchema.get();
    }
};
