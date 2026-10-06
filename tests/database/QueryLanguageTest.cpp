#include <optional>
#include <string>
#include <vector>

#include "DatabaseTest.hpp"

using namespace orm::query;

class QueryLanguageTest : public DatabaseTest<models::Schema>
{
};

TEST_P(QueryLanguageTest, whereWithComparisonAndLike_shouldFilterRows)
{
    createTable<models::SomeDataModel>();
    database.insert(std::vector<models::SomeDataModel>{{1, "miss", 1.0}, {2, "match-one", 2.0}, {3, "match-two", 3.0}});

    orm::Query<models::SomeDataModel> query;
    query.where(col<&models::SomeDataModel::field1>() >= 2 && col<&models::SomeDataModel::field2>().like("match%"))
        .orderBy(asc(col<&models::SomeDataModel::field1>()));

    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 2);
    EXPECT_EQ(returnedModels[0].field1, 2);
    EXPECT_EQ(returnedModels[1].field1, 3);
}

TEST_P(QueryLanguageTest, whereWithOptionalNull_shouldFilterRows)
{
    createTable<models::ModelWithOptional>();
    database.insert(std::vector<models::ModelWithOptional>{{std::nullopt, "null", 1.0}, {2, "not-null", 2.0}});

    orm::Query<models::ModelWithOptional> query;
    query.where(col<&models::ModelWithOptional::field1>().isNull());

    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_FALSE(returnedModels[0].field1.has_value());
    ASSERT_TRUE(returnedModels[0].field2.has_value());
    EXPECT_EQ(returnedModels[0].field2.value(), "null");
}

TEST_P(QueryLanguageTest, whereWithSqlInjectionLikeValue_shouldTreatValueAsParameter)
{
    createTable<models::SomeDataModel>();
    const std::string injectedValue{"x' OR 1=1 --"};
    database.insert(std::vector<models::SomeDataModel>{{1, injectedValue, 1.0}, {2, "safe", 2.0}});

    orm::Query<models::SomeDataModel> query;
    query.where(col<&models::SomeDataModel::field2>() == injectedValue);

    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_EQ(returnedModels[0].field1, 1);
    EXPECT_EQ(returnedModels[0].field2, injectedValue);
}

TEST_P(QueryLanguageTest, orderByWithLimitAndOffset_shouldReturnExpectedPage)
{
    createTable<models::SomeDataModel>();
    database.insert(std::vector<models::SomeDataModel>{{1, "one", 1.0}, {2, "two", 2.0}, {3, "three", 3.0}});

    orm::Query<models::SomeDataModel> query;
    query.orderBy(desc(col<&models::SomeDataModel::field1>())).limit(1).offset(1);

    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_EQ(returnedModels[0].field1, 2);
}

TEST_P(QueryLanguageTest, orderByWithOffsetOnly_shouldReturnRemainingRows)
{
    createTable<models::SomeDataModel>();
    database.insert(std::vector<models::SomeDataModel>{{1, "one", 1.0}, {2, "two", 2.0}, {3, "three", 3.0}});

    orm::Query<models::SomeDataModel> query;
    query.orderBy(asc(col<&models::SomeDataModel::field1>())).offset(1);

    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 2);
    EXPECT_EQ(returnedModels[0].field1, 2);
    EXPECT_EQ(returnedModels[1].field1, 3);
}

TEST_P(QueryLanguageTest, whereWithRelatedFieldPath_shouldFilterJoinedRows)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();
    const auto models = std::vector<models::ModelWithId>{{1, 1, "first"}, {2, 2, "second"}};
    const auto relatedModels = std::vector<models::ModelRelatedToOtherModel>{{1, 10, "related-first", models[0]},
                                                                             {2, 20, "related-second", models[1]}};
    database.insert(models);
    database.insert(relatedModels);

    orm::Query<models::ModelRelatedToOtherModel> query;
    query.where(col<&models::ModelRelatedToOtherModel::field3, &models::ModelWithId::field2>() == "second");

    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_EQ(returnedModels[0].id, 2);
    EXPECT_EQ(returnedModels[0].field3.id, 2);
    EXPECT_EQ(returnedModels[0].field3.field2, "second");
}

TEST_P(QueryLanguageTest, whereWithCompositeRelatedFieldPath_shouldJoinByAllIds)
{
    createTable<models::ModelWithOverwrittenId>();
    createTable<models::ModelRelatedToCompositeIdModel>();
    const auto models = std::vector<models::ModelWithOverwrittenId>{{10, 1, "first"}, {20, 2, "second"}};
    const auto relatedModels = std::vector<models::ModelRelatedToCompositeIdModel>{{1, "related-first", models[0]},
                                                                                   {2, "related-second", models[1]}};
    database.insert(models);
    database.insert(relatedModels);

    orm::Query<models::ModelRelatedToCompositeIdModel> query;
    query.where(col<&models::ModelRelatedToCompositeIdModel::field3, &models::ModelWithOverwrittenId::field2>() ==
                "second");

    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_EQ(returnedModels[0].id, 2);
    EXPECT_EQ(returnedModels[0].field3.field1, 2);
    EXPECT_EQ(returnedModels[0].field3.field2, "second");
}

TEST_P(QueryLanguageTest, fullModelGroupByHaving_shouldFilterAndPageGroups)
{
    createTable<models::ModelWithId>();
    database.insert(std::vector<models::ModelWithId>{
        {1, 10, "alpha"}, {2, 20, "alpha"}, {3, 30, "beta"}, {4, 40, "beta"}, {5, 50, "gamma"}, {6, 60, "gamma"}});

    orm::Query<models::ModelWithId> query;
    query.where(col<&models::ModelWithId::id>() >= 1)
        .groupBy(col<&models::ModelWithId::field2>())
        .having(countAll<models::ModelWithId>() == 2)
        .andHaving(count(col<&models::ModelWithId::field1>()) == 2)
        .andHaving(sum(col<&models::ModelWithId::field1>()) > 20)
        .andHaving(avg(col<&models::ModelWithId::field1>()) >= 15.0)
        .andHaving(min(col<&models::ModelWithId::field1>()) >= 10)
        .andHaving(max(col<&models::ModelWithId::field1>()) <= 60)
        .orderBy(asc(col<&models::ModelWithId::field2>()))
        .limit(1)
        .offset(1);

    const std::vector<models::ModelWithId> returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_EQ(returnedModels[0].field2, "beta");
}

TEST_P(QueryLanguageTest, fullModelGroupByHaving_shouldSupportRelatedPaths)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();
    const auto relatedModels = std::vector<models::ModelWithId>{{1, 100, "group-a"}, {2, 200, "group-b"}};
    const auto models = std::vector<models::ModelRelatedToOtherModel>{
        {1, 10, "first", relatedModels[0]}, {2, 20, "second", relatedModels[0]}, {3, 30, "third", relatedModels[1]}};
    database.insert(relatedModels);
    database.insert(models);

    orm::Query<models::ModelRelatedToOtherModel> query;
    query.groupBy(col<&models::ModelRelatedToOtherModel::field3, &models::ModelWithId::field2>())
        .having(countAll<models::ModelRelatedToOtherModel>() > 1)
        .andHaving(avg(col<&models::ModelRelatedToOtherModel::field1>()) >= 15.0);

    const std::vector<models::ModelRelatedToOtherModel> returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_EQ(returnedModels[0].field3.id, 1);
    EXPECT_EQ(returnedModels[0].field3.field2, "group-a");
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, QueryLanguageTest, backendTestConfigs, backendTestName);
