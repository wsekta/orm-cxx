#pragma once

#include <cstddef>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "orm-cxx-tests/TestConfig.hpp"
#include "tests/database/postgresql/PostgresqlTestSchema.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/GenerateModels.hpp"

struct BackendTestConfig
{
    orm::db::BackendType type;
    std::string name;
    std::string connectionString;
    bool supported;
};

namespace orm::test::fixtures
{
inline constexpr std::size_t modelCount = 10;

inline const auto sqliteBackendTestConfig = BackendTestConfig{
    .type = orm::db::BackendType::Sqlite,
    .name = "Sqlite",
    .connectionString = "sqlite3://:memory:",
    .supported = true,
};

inline const auto postgresqlBackendTestConfig = BackendTestConfig{
    .type = orm::db::BackendType::Postgres,
    .name = "Postgresql",
    .connectionString = {},
    .supported = true,
};

// Keep the legacy integration fixtures SQLite-only. Backends added to the
// reusable public conformance profile must not implicitly run suites that use
// SQLite-specific assumptions or private driver instrumentation.
[[maybe_unused]] inline const auto backendTestConfigs = ::testing::Values(sqliteBackendTestConfig);
[[maybe_unused]] inline const auto conformanceBackendTestConfigs = ::testing::ValuesIn(
    []
    {
        std::vector<BackendTestConfig> configs{sqliteBackendTestConfig};
        if constexpr (orm::test::config::postgresqlIntegrationEnabled)
        {
            configs.push_back(postgresqlBackendTestConfig);
        }
        return configs;
    }());

auto backendTestName(const ::testing::TestParamInfo<BackendTestConfig>& info) -> std::string
{
    return info.param.name;
}
} // namespace

template <typename SchemaType = models::Schema>
class DatabaseTest : public ::testing::TestWithParam<BackendTestConfig>
{
public:
    orm::Database<SchemaType> database;

    auto SetUp() -> void override
    {
        activeConnectionString = GetParam().connectionString;
        if constexpr (orm::test::config::postgresqlIntegrationEnabled)
        {
            if (GetParam().type == orm::db::BackendType::Postgres)
            {
                const auto dsn = postgresql_test::configuredDsn();
                ASSERT_FALSE(dsn.empty())
                    << "ORM_CXX_POSTGRESQL_TEST_DSN is required when PostgreSQL integration tests are enabled";

                try
                {
                    postgresqlSchema = std::make_unique<postgresql_test::PostgresqlTestSchema>(dsn);
                    activeConnectionString = postgresqlSchema->connectionString();
                }
                catch (const std::exception& error)
                {
                    FAIL() << "Failed to create an isolated PostgreSQL test schema: " << error.what();
                }
            }
        }

        database.connect(GetParam().type, activeConnectionString);
    }

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

        if (database.isConnected())
        {
            try
            {
                database.disconnect();
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

    [[nodiscard]] auto testConnectionString() const noexcept -> const std::string&
    {
        return activeConnectionString;
    }

    auto postgresqlTestSchema() noexcept -> postgresql_test::PostgresqlTestSchema*
    {
        return postgresqlSchema.get();
    }

private:
    std::vector<std::function<void()>> tearDownFunctions;
    std::string activeConnectionString;
    std::unique_ptr<postgresql_test::PostgresqlTestSchema> postgresqlSchema;
};
