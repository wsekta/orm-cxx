#pragma once

#include <gtest/gtest.h>

#include "faker-cxx/Lorem.h"
#include "faker-cxx/Number.h"
#include "orm-cxx/database.hpp"
#include "orm-cxx/query.hpp"
#include "tests/ModelsDefinitions.hpp"

namespace
{
const std::string sqliteConnectionString = "sqlite3://:memory:";
const std::size_t modelCount = 10;

auto connectionStrings = ::testing::Values(sqliteConnectionString);
}

template <typename T>
auto generateSomeDataModels(std::size_t count) -> std::vector<T>
{
    std::vector<T> result;

    for (std::size_t i = 0; i < count; i++)
    {
        result.emplace_back(faker::Number::integer(512), faker::Lorem::word(), faker::Number::decimal(-1.0, 1.0));
    }

    return result;
}

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
