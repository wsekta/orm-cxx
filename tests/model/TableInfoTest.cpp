#include "orm-cxx/model/TableInfo.hpp"

#include <gtest/gtest.h>

#include "tests/ModelsDefinitions.hpp"

using namespace std::string_view_literals;
using namespace orm::model;

struct LocalModel
{
    int id;
    std::string field1;
    [[maybe_unused]] double field2;
};

TEST(TableInfoTest, shouldGetTableName)
{
    static_assert(getTableName<models::ModelWithId>() == "models_ModelWithId"sv);

    EXPECT_EQ(getTableName<models::ModelWithId>(), "models_ModelWithId");
}

TEST(TableInfoTest, shouldGetTableNameFromModelWithTableName)
{
    static_assert(getTableName<models::ModelWithTableName>() == "some_table_name"sv);

    EXPECT_EQ(getTableName<models::ModelWithTableName>(), "some_table_name");
}

TEST(TableInfoTest, shouldGetTableNameFromLocalModel)
{
    static_assert(getTableName<LocalModel>() == "LocalModel"sv);

    EXPECT_EQ(getTableName<LocalModel>(), "LocalModel");
}
