#include "DatabaseTest.hpp"

class IdModelsTest : public DatabaseTest
{
};

TEST_P(IdModelsTest, shouldCreateTableWithIdColumn)
{
    EXPECT_NO_THROW(createTable<models::ModelWithId>());
}

TEST_P(IdModelsTest, shouldCreateTableWithOverwrittenIdColumn)
{
    EXPECT_NO_THROW(createTable<models::ModelWithOverwrittenId>());
}

TEST_P(IdModelsTest, shouldExecuteInsertQueryAndSelectQueryWithNamesMapping_valuesShouldBeSame)
{
    createTable<models::ModelWithIdAndNamesMapping>();
    auto models = std::vector<models::ModelWithIdAndNamesMapping>{{1, 1, "test"}, {2, 2, "test2"}};
    database.insert(models);
    orm::Query<models::ModelWithIdAndNamesMapping> queryForNamesMapping;
    auto returnedModels = database.select(queryForNamesMapping);

    for (std::size_t i = 0; i < models.size(); i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, IdModelsTest, connectionStrings);
