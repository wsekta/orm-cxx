#include <optional>
#include <string>
#include <vector>

#include "DatabaseTest.hpp"

using namespace orm::query;

class WriteOperationsTest : public DatabaseTest
{
};

TEST_P(WriteOperationsTest, updateSingleRow_shouldChangeOnlyMatchingRow)
{
    createTable<models::SomeDataModel>();
    database.insert(std::vector<models::SomeDataModel>{{1, "one", 1.0}, {2, "two", 2.0}});

    orm::Update<models::SomeDataModel> update;
    update.set(col("field2"), "updated").where(col("field1") == 1);

    const auto affectedRows = database.update(update);

    orm::Query<models::SomeDataModel> query;
    query.where(col("field1") == 1);
    const auto returnedModels = database.select(query);

    ASSERT_EQ(affectedRows, 1);
    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_EQ(returnedModels[0].field2, "updated");
}

TEST_P(WriteOperationsTest, updateMultipleRows_shouldReturnAffectedRows)
{
    createTable<models::SomeDataModel>();
    database.insert(
        std::vector<models::SomeDataModel>{{1, "one", 1.0}, {2, "two", 2.0}, {3, "three", 3.0}});

    orm::Update<models::SomeDataModel> update;
    update.set(col("field2"), "updated").where(col("field1") >= 2);

    const auto affectedRows = database.update(update);

    orm::Query<models::SomeDataModel> query;
    query.where(col("field2") == "updated");
    const auto returnedModels = database.select(query);

    ASSERT_EQ(affectedRows, 2);
    EXPECT_EQ(returnedModels.size(), 2);
}

TEST_P(WriteOperationsTest, updateToNull_shouldStoreNullForOptionalField)
{
    createTable<models::ModelWithOptional>();
    database.insert(std::vector<models::ModelWithOptional>{{1, "one", 1.0}, {2, "two", 2.0}});

    orm::Update<models::ModelWithOptional> update;
    update.set(col("field1"), std::nullopt).where(col("field2") == "two");

    const auto affectedRows = database.update(update);

    orm::Query<models::ModelWithOptional> query;
    query.where(col("field2") == "two");
    const auto returnedModels = database.select(query);

    ASSERT_EQ(affectedRows, 1);
    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_FALSE(returnedModels[0].field1.has_value());
}

TEST_P(WriteOperationsTest, updateWithNoMatches_shouldReturnZero)
{
    createTable<models::SomeDataModel>();
    database.insert(std::vector<models::SomeDataModel>{{1, "one", 1.0}});

    orm::Update<models::SomeDataModel> update;
    update.set(col("field2"), "updated").where(col("field1") == 99);

    EXPECT_EQ(database.update(update), 0);
}

TEST_P(WriteOperationsTest, removeMatchingRow_shouldDeleteOnlyMatchingRow)
{
    createTable<models::SomeDataModel>();
    database.insert(std::vector<models::SomeDataModel>{{1, "one", 1.0}, {2, "two", 2.0}});

    const auto affectedRows = database.remove<models::SomeDataModel>(col("field1") == 2);

    orm::Query<models::SomeDataModel> query;
    const auto returnedModels = database.select(query);

    ASSERT_EQ(affectedRows, 1);
    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_EQ(returnedModels[0].field1, 1);
}

TEST_P(WriteOperationsTest, removeWithRawPredicateWithoutParameters_shouldReturnAffectedRows)
{
    createTable<models::SomeDataModel>();
    database.insert(std::vector<models::SomeDataModel>{{1, "one", 1.0}, {2, "two", 2.0}});

    const auto affectedRows = database.remove<models::SomeDataModel>(raw("field1 = field1"));

    orm::Query<models::SomeDataModel> query;
    const auto returnedModels = database.select(query);

    ASSERT_EQ(affectedRows, 2);
    EXPECT_TRUE(returnedModels.empty());
}

TEST_P(WriteOperationsTest, updateWithSqlInjectionLikeValue_shouldTreatValueAsParameter)
{
    createTable<models::SomeDataModel>();
    const std::string injectedValue{"x', field1 = 999 --"};
    database.insert(std::vector<models::SomeDataModel>{{1, "one", 1.0}, {2, "two", 2.0}});

    orm::Update<models::SomeDataModel> update;
    update.set(col("field2"), injectedValue).where(col("field1") == 1);

    const auto affectedRows = database.update(update);

    orm::Query<models::SomeDataModel> query;
    query.orderBy(asc(col("field1")));
    const auto returnedModels = database.select(query);

    ASSERT_EQ(affectedRows, 1);
    ASSERT_EQ(returnedModels.size(), 2);
    EXPECT_EQ(returnedModels[0].field1, 1);
    EXPECT_EQ(returnedModels[0].field2, injectedValue);
    EXPECT_EQ(returnedModels[1].field1, 2);
    EXPECT_EQ(returnedModels[1].field2, "two");
}

TEST_P(WriteOperationsTest, updateRelatedPrimaryKey_shouldChangeForeignKey)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();
    const auto models = std::vector<models::ModelWithId>{{1, 10, "first"}, {2, 20, "second"}};
    database.insert(models);
    database.insert(models::ModelRelatedToOtherModel{1, 100, "related", models[0]});

    orm::Update<models::ModelRelatedToOtherModel> update;
    update.set(col("field3.id"), 2).where(col("id") == 1);

    const auto affectedRows = database.update(update);

    orm::Query<models::ModelRelatedToOtherModel> query;
    query.where(col("id") == 1);
    const auto returnedModels = database.select(query);

    ASSERT_EQ(affectedRows, 1);
    ASSERT_EQ(returnedModels.size(), 1);
    EXPECT_EQ(returnedModels[0].field3.id, 2);
    EXPECT_EQ(returnedModels[0].field3.field2, "second");
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, WriteOperationsTest, connectionStrings);
