#include "DatabaseTest.hpp"

class RelatedModelTest : public DatabaseTest
{
};

TEST_P(RelatedModelTest, shouldCreateTableWithRelatedModel)
{
    createTable<models::ModelWithId>();

    EXPECT_NO_THROW(createTable<models::ModelRelatedToOtherModel>());
}

TEST_P(RelatedModelTest, shouldExecuteInsertQueryAndSelectQueryWithRelatedModel_valuesShouldBeSame)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();
    auto models = std::vector<models::ModelWithId>{{1, 1, ""}, {2, 2, ""}};
    auto relatedModels = std::vector<models::ModelRelatedToOtherModel>{{1, 1, "", models[0]}, {2, 2, "", models[1]}};
    database.insert(models);
    database.insert(relatedModels);
    orm::Query<models::ModelRelatedToOtherModel> queryForRelatedModel;
    auto returnedModels = database.select(queryForRelatedModel);

    for (std::size_t i = 0; i < models.size(); i++)
    {
        EXPECT_EQ(relatedModels[i].field1, returnedModels[i].field1);
        EXPECT_EQ(relatedModels[i].field2, returnedModels[i].field2);
        EXPECT_EQ(relatedModels[i].field3.id, returnedModels[i].field3.id);
        EXPECT_EQ(relatedModels[i].field3.field1, returnedModels[i].field3.field1);
        EXPECT_EQ(relatedModels[i].field3.field2, returnedModels[i].field3.field2);
    }
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, RelatedModelTest, connectionStrings);
