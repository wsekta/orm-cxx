#include "DatabaseTest.hpp"

using namespace orm::query;

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

TEST_P(IdModelsTest, shouldExecuteInsertQueryAndSelectQueryWithModelWithId_valuesShouldBeSame)
{
    createTable<models::ModelWithId>();
    auto models = std::vector<models::ModelWithId>{{1, 1, ""}, {2, 2, ""}};
    database.insert(models);
    orm::Query<models::ModelWithId> queryForIdModel;
    auto returnedModels = database.select(queryForIdModel);

    for (std::size_t i = 0; i < models.size(); i++)
    {
        EXPECT_EQ(models[i].id, returnedModels[i].id);
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }
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
        EXPECT_EQ(models[i].id, returnedModels[i].id);
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }
}

TEST_P(IdModelsTest, shouldGenerateAutoIncrementIdsOnInsert)
{
    createTable<models::ModelWithAutoIncrementId>();
    const auto models = std::vector<models::ModelWithAutoIncrementId>{{0, 10, "first"}, {0, 20, "second"}};

    database.insert(models);

    orm::Query<models::ModelWithAutoIncrementId> query;
    query.orderBy(asc(col("id")));
    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 2);
    EXPECT_EQ(returnedModels[0].id, 1);
    EXPECT_EQ(returnedModels[0].field1, 10);
    EXPECT_EQ(returnedModels[0].field2, "first");
    EXPECT_EQ(returnedModels[1].id, 2);
    EXPECT_EQ(returnedModels[1].field1, 20);
    EXPECT_EQ(returnedModels[1].field2, "second");
}

TEST_P(IdModelsTest, shouldUseGeneratedAutoIncrementIdsInWriteQueries)
{
    createTable<models::ModelWithAutoIncrementId>();
    database.insert(std::vector<models::ModelWithAutoIncrementId>{{0, 10, "first"}, {0, 20, "second"}});

    orm::Query<models::ModelWithAutoIncrementId> initialQuery;
    initialQuery.orderBy(asc(col("id")));
    const auto initialModels = database.select(initialQuery);
    ASSERT_EQ(initialModels.size(), 2);

    orm::Update<models::ModelWithAutoIncrementId> update;
    update.set(col("field2"), "updated").where(col("id") == initialModels[1].id);
    EXPECT_EQ(database.update(update), 1);
    EXPECT_EQ(database.remove<models::ModelWithAutoIncrementId>(col("id") == initialModels[0].id), 1);

    orm::Query<models::ModelWithAutoIncrementId> query;
    query.orderBy(asc(col("id")));
    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_EQ(returnedModels[0].id, initialModels[1].id);
    EXPECT_EQ(returnedModels[0].field1, 20);
    EXPECT_EQ(returnedModels[0].field2, "updated");
}

TEST_P(IdModelsTest, shouldUseGeneratedAutoIncrementIdInRelatedModel)
{
    createTable<models::ModelWithAutoIncrementId>();
    database.insert(std::vector<models::ModelWithAutoIncrementId>{{0, 10, "first"}, {0, 20, "second"}});

    orm::Query<models::ModelWithAutoIncrementId> idQuery;
    idQuery.orderBy(asc(col("id")));
    const auto generatedModels = database.select(idQuery);
    ASSERT_EQ(generatedModels.size(), 2);

    createTable<models::ModelRelatedToAutoIncrementModel>();
    database.insert(models::ModelRelatedToAutoIncrementModel{1, 100, generatedModels[1]});

    orm::Query<models::ModelRelatedToAutoIncrementModel> query;
    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_EQ(returnedModels[0].id, 1);
    EXPECT_EQ(returnedModels[0].field1, 100);
    EXPECT_EQ(returnedModels[0].field3.id, generatedModels[1].id);
    EXPECT_EQ(returnedModels[0].field3.field1, generatedModels[1].field1);
    EXPECT_EQ(returnedModels[0].field3.field2, generatedModels[1].field2);
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, IdModelsTest, connectionStrings);
