#include "orm-cxx/model/TableInfo.hpp"

#include <gtest/gtest.h>

#include "tests/ModelsDefinitions.hpp"

struct LocalModel
{
    int id;
    std::string field1;
    [[maybe_unused]] double field2;
};

TEST(TableInfoTest, shouldGetTableName)
{
    EXPECT_EQ(orm::model::getTableName<models::ModelWithId>(), "models_ModelWithId");
}

TEST(TableInfoTest, shouldGetTableNameFromModelWithTableName)
{
    EXPECT_EQ(orm::model::getTableName<models::ModelWithTableName>(), "some_table_name");
}

TEST(TableInfoTest, shouldGetTableNameFromLocalModel)
{
    EXPECT_EQ(orm::model::getTableName<LocalModel>(), "LocalModel");
}
