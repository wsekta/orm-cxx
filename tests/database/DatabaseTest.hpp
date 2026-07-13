#pragma once

#include <cstddef>
#include <functional>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "orm-cxx/database.hpp"
#include "orm-cxx/query.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/GenerateModels.hpp"

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

// Keep the legacy integration fixtures SQLite-only. Backends added to the
// reusable public conformance profile must not implicitly run suites that use
// SQLite-specific assumptions or private driver instrumentation.
[[maybe_unused]] const auto backendTestConfigs = ::testing::Values(sqliteBackendTestConfig);
[[maybe_unused]] const auto conformanceBackendTestConfigs = ::testing::Values(sqliteBackendTestConfig);

auto backendTestName(const ::testing::TestParamInfo<BackendTestConfig>& info) -> std::string
{
    return info.param.name;
}
} // namespace

using orm::generateSomeDataModels;

class DatabaseTest : public ::testing::TestWithParam<BackendTestConfig>
{
public:
    orm::Database database;

    auto SetUp() -> void override
    {
        database.connect(GetParam().type, GetParam().connectionString);
    }

    auto TearDown() -> void override
    {
        for (auto tearDownFunction = tearDownFunctions.rbegin(); tearDownFunction != tearDownFunctions.rend();
             ++tearDownFunction)
        {
            (*tearDownFunction)();
        }

        if (database.isConnected())
        {
            database.disconnect();
        }
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

private:
    std::vector<std::function<void()>> tearDownFunctions;
};
