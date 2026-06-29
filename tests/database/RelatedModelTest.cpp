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
    queryForRelatedModel.disableJoining();
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

TEST_P(RelatedModelTest, shouldRoundTripOptionalRelatedModelWithJoining)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelOptionallyRelatedToOtherModel>();
    const auto relatedModels = std::vector<models::ModelWithId>{{1, 10, "first"}, {2, 20, "second"}};
    database.insert(relatedModels);
    database.insert(std::vector<models::ModelOptionallyRelatedToOtherModel>{
        {1, 100, "with-relation", relatedModels[0]}, {2, 200, "without-relation", std::nullopt}});

    orm::Query<models::ModelOptionallyRelatedToOtherModel> query;
    query.orderBy(orm::query::asc(orm::query::col("id")));
    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 2);
    EXPECT_EQ(returnedModels[0].id, 1);
    ASSERT_TRUE(returnedModels[0].field3.has_value());
    EXPECT_EQ(returnedModels[0].field3->id, relatedModels[0].id);
    EXPECT_EQ(returnedModels[0].field3->field1, relatedModels[0].field1);
    EXPECT_EQ(returnedModels[0].field3->field2, relatedModels[0].field2);
    EXPECT_EQ(returnedModels[1].id, 2);
    EXPECT_FALSE(returnedModels[1].field3.has_value());
}

TEST_P(RelatedModelTest, shouldRoundTripOptionalRelatedModelWithoutJoining)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelOptionallyRelatedToOtherModel>();
    const auto relatedModel = models::ModelWithId{1, 10, "first"};
    database.insert(relatedModel);
    database.insert(std::vector<models::ModelOptionallyRelatedToOtherModel>{
        {1, 100, "with-relation", relatedModel}, {2, 200, "without-relation", std::nullopt}});

    orm::Query<models::ModelOptionallyRelatedToOtherModel> query;
    query.disableJoining().orderBy(orm::query::asc(orm::query::col("id")));
    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 2);
    EXPECT_EQ(returnedModels[0].id, 1);
    ASSERT_TRUE(returnedModels[0].field3.has_value());
    EXPECT_EQ(returnedModels[0].field3->id, relatedModel.id);
    EXPECT_EQ(returnedModels[1].id, 2);
    EXPECT_FALSE(returnedModels[1].field3.has_value());
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, RelatedModelTest, connectionStrings);
