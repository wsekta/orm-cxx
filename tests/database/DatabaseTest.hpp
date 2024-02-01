#pragma once

#include <gtest/gtest.h>

#include "orm-cxx/database.hpp"
#include "orm-cxx/query.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/GenerateModels.hpp"

namespace
{
const std::string sqliteConnectionString = "sqlite3://:memory:";
const std::size_t modelCount = 10;

auto connectionStrings = ::testing::Values(sqliteConnectionString);
} // namespace

using orm::generateSomeDataModels;

class DatabaseTest : public ::testing::TestWithParam<std::string>
{
public:
    orm::Database database;

    auto SetUp() -> void override
    {
        database.connect(GetParam());
    }

    auto TearDown() -> void override
    {
        for (auto& tearDownFunction : tearDownFunctions)
        {
            tearDownFunction();
        }

        database.disconnect();
    }

    template <typename T>
    auto createTable() -> void
    {
        database.createTable<T>();
        tearDownFunctions.emplace_back([this]() { database.deleteTable<T>(); });
    }

private:
    std::vector<std::function<void()>> tearDownFunctions;
};
