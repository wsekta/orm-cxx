#include "DatabaseTest.hpp"

class RelatedModelTest : public DatabaseTest
{
};

TEST_P(RelatedModelTest, shouldCreateTableWithRelatedModel)
{
    createTable<models::ModelWithId>();

    EXPECT_NO_THROW(createTable<models::ModelRelatedToOtherModel>());
}

TEST_P(RelatedModelTest,
       shouldExecuteInsertQueryAndSelectQueryWithRelatedModel_withoutJoining_onlyIdsOfRelatedModelShouldBeTheSame)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();
    const auto models = std::vector<models::ModelWithId>{{1, 1, ""}, {2, 2, ""}};
    const auto relatedModels =
        std::vector<models::ModelRelatedToOtherModel>{{1, 1, "", models[0]}, {2, 2, "", models[1]}};
    database.insert(models);
    database.insert(relatedModels);
    orm::Query<models::ModelRelatedToOtherModel> queryForRelatedModel;
    auto returnedModels = database.select(queryForRelatedModel);

    for (std::size_t i = 0; i < models.size(); i++)
    {
        EXPECT_EQ(relatedModels[i].field1, returnedModels[i].field1);
        EXPECT_EQ(relatedModels[i].field2, returnedModels[i].field2);
        EXPECT_EQ(relatedModels[i].field3.id, returnedModels[i].field3.id);
    }
}

TEST_P(RelatedModelTest, shouldExecuteInsertQueryAndSelectQueryWithRelatedModel_withJoining_allValuesShouldBeTheSame)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();
    const auto models = std::vector<models::ModelWithId>{{3, 3, ""}, {4, 4, ""}};
    const auto relatedModels =
        std::vector<models::ModelRelatedToOtherModel>{{1, 1, "", models[0]}, {2, 2, "", models[1]}};
    database.insert(models);
    database.insert(relatedModels);
    orm::Query<models::ModelRelatedToOtherModel> queryForRelatedModel;
    queryForRelatedModel.joinRelated();
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
