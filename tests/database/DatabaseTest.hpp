#pragma once

#include <cstddef>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "orm-cxx/database.hpp"
#include "orm-cxx/query.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/GenerateModels.hpp"

#ifndef ORM_CXX_ENABLE_POSTGRESQL_BACKEND
#define ORM_CXX_ENABLE_POSTGRESQL_BACKEND 0
#endif

#ifndef ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS
#define ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS 0
#endif

#if ORM_CXX_ENABLE_POSTGRESQL_BACKEND && ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS
#include "tests/database/postgresql/PostgresqlTestSchema.hpp"
#endif

struct BackendTestConfig
{
    orm::db::BackendType type;
    std::string name;
    std::string connectionString;
    bool supported;
};

namespace
{
const std::size_t modelCount = 10;

const auto sqliteBackendTestConfig = BackendTestConfig{
    .type = orm::db::BackendType::Sqlite,
    .name = "Sqlite",
    .connectionString = "sqlite3://:memory:",
    .supported = true,
};

#if ORM_CXX_ENABLE_POSTGRESQL_BACKEND && ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS
const auto postgresqlBackendTestConfig = BackendTestConfig{
    .type = orm::db::BackendType::Postgres,
    .name = "Postgresql",
    .connectionString = {},
    .supported = true,
};
#endif

// Keep the legacy integration fixtures SQLite-only. Backends added to the
// reusable public conformance profile must not implicitly run suites that use
// SQLite-specific assumptions or private driver instrumentation.
[[maybe_unused]] const auto backendTestConfigs = ::testing::Values(sqliteBackendTestConfig);
#if ORM_CXX_ENABLE_POSTGRESQL_BACKEND && ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS
[[maybe_unused]] const auto conformanceBackendTestConfigs =
    ::testing::Values(sqliteBackendTestConfig, postgresqlBackendTestConfig);
#else
[[maybe_unused]] const auto conformanceBackendTestConfigs = ::testing::Values(sqliteBackendTestConfig);
#endif

auto backendTestName(const ::testing::TestParamInfo<BackendTestConfig>& info) -> std::string
{
    return info.param.name;
}
} // namespace

using orm::generateSomeDataModels;

template <typename SchemaType = models::Schema>
class DatabaseTest : public ::testing::TestWithParam<BackendTestConfig>
{
public:
    orm::Database<SchemaType> database;

    auto SetUp() -> void override
    {
#if ORM_CXX_ENABLE_POSTGRESQL_BACKEND && ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS
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
        else
#endif
        {
            activeConnectionString = GetParam().connectionString;
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

#if ORM_CXX_ENABLE_POSTGRESQL_BACKEND && ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS
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
#endif
    }

    template <typename T>
    auto createTable() -> void
    {
        database.createTable<T>();
        tearDownFunctions.emplace_back([this]() { database.deleteTable<T>(); });
    }

    template <typename T>
    auto createRelationTables() -> void
    {
        database.createRelationTables<T>();
        tearDownFunctions.emplace_back([this]() { database.deleteRelationTables<T>(); });
    }

    [[nodiscard]] auto testConnectionString() const noexcept -> const std::string&
    {
        return activeConnectionString;
    }

#if ORM_CXX_ENABLE_POSTGRESQL_BACKEND && ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS
    auto postgresqlTestSchema() noexcept -> postgresql_test::PostgresqlTestSchema*
    {
        return postgresqlSchema.get();
    }
#endif

private:
    std::vector<std::function<void()>> tearDownFunctions;
    std::string activeConnectionString;
#if ORM_CXX_ENABLE_POSTGRESQL_BACKEND && ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS
    std::unique_ptr<postgresql_test::PostgresqlTestSchema> postgresqlSchema;
#endif
};
