#include "DefaultSelectCommand.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <variant>

#include "orm-cxx/query.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/FakeDatabase.hpp"

using namespace orm::query;

namespace
{
const std::string selectSql = "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
                              "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
                              "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
                              "FROM models_ModelWithFloat;";

const std::string selectSqlWithLimit = "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
                                       "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
                                       "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
                                       "FROM models_ModelWithFloat LIMIT 10;";

const std::string selectSqlWithOffset = "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
                                        "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
                                        "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
                                        "FROM models_ModelWithFloat OFFSET 10;";

const std::string selectSqlWithLimitAndOffset = "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
                                                "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
                                                "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
                                                "FROM models_ModelWithFloat LIMIT 10 OFFSET 10;";

const std::string selectSqlWithModelRelatedToOtherModelWithoutJoining =
    "SELECT models_ModelRelatedToOtherModel.id AS models_ModelRelatedToOtherModel_id, "
    "models_ModelRelatedToOtherModel.field1 AS models_ModelRelatedToOtherModel_field1, "
    "models_ModelRelatedToOtherModel.field2 AS models_ModelRelatedToOtherModel_field2, "
    "models_ModelRelatedToOtherModel.field3_id AS models_ModelRelatedToOtherModel_field3_id"
    " FROM models_ModelRelatedToOtherModel;";

const std::string selectSqlWithModelRelatedToOtherModelWithJoining =
    "SELECT models_ModelRelatedToOtherModel.id AS models_ModelRelatedToOtherModel_id, "
    "models_ModelRelatedToOtherModel.field1 AS models_ModelRelatedToOtherModel_field1, "
    "models_ModelRelatedToOtherModel.field2 AS models_ModelRelatedToOtherModel_field2, "
    "field3.id AS field3_id, field3.field1 AS field3_field1, field3.field2 AS field3_field2"
    " FROM models_ModelRelatedToOtherModel"
    " LEFT JOIN models_ModelWithId AS field3 ON field3.id = models_ModelRelatedToOtherModel.field3_id;";

const std::string selectSqlWithModelRelatedToCompositeIdModelWithJoining =
    "SELECT models_ModelRelatedToCompositeIdModel.id AS models_ModelRelatedToCompositeIdModel_id, "
    "models_ModelRelatedToCompositeIdModel.field1 AS models_ModelRelatedToCompositeIdModel_field1, "
    "field3.id AS field3_id, field3.field1 AS field3_field1, field3.field2 AS field3_field2"
    " FROM models_ModelRelatedToCompositeIdModel"
    " LEFT JOIN models_ModelWithOverwrittenId AS field3 ON "
    "field3.field1 = models_ModelRelatedToCompositeIdModel.field3_field1 AND "
    "field3.field2 = models_ModelRelatedToCompositeIdModel.field3_field2;";

template <typename T>
auto getValue(const orm::query::QueryParameter& parameter) -> T
{
    return std::get<T>(parameter.value.get());
}
} // namespace

class DefaultSelectCommandTest : public ::testing::Test
{
public:
    orm::db::commands::DefaultSelectCommand command;
};

TEST_F(DefaultSelectCommandTest, select)
{
    orm::Query<models::ModelWithFloat> query;

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql, selectSql);
}

TEST_F(DefaultSelectCommandTest, selectWithLimit)
{
    orm::Query<models::ModelWithFloat> query;

    query.limit(10);

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql, selectSqlWithLimit);
}

TEST_F(DefaultSelectCommandTest, selectWithOffset)
{
    orm::Query<models::ModelWithFloat> query;

    query.offset(10);

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql, selectSqlWithOffset);
}

TEST_F(DefaultSelectCommandTest, selectWithLimitAndOffset)
{
    orm::Query<models::ModelWithFloat> query;

    query.limit(10).offset(10);

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql, selectSqlWithLimitAndOffset);

    orm::Query<models::ModelWithFloat> query2;

    query2.offset(10).limit(10);

    EXPECT_EQ(command.select(orm::Database::getQueryData(query2)).sql, selectSqlWithLimitAndOffset);
}

TEST_F(DefaultSelectCommandTest, selectWithDistinct)
{
    orm::Query<models::ModelWithFloat> query;

    query.distinct();

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              "SELECT DISTINCT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
              "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
              "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 FROM models_ModelWithFloat;");
}

TEST_F(DefaultSelectCommandTest, selectWithComparisonPredicate_shouldUseBindParameter)
{
    orm::Query<models::ModelWithFloat> query;

    query.where(col("field1") == 5);

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql, "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
                             "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
                             "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
                             "FROM models_ModelWithFloat WHERE models_ModelWithFloat.field1 = :orm_p0;");
    ASSERT_EQ(statement.parameters.size(), 1);
    EXPECT_EQ(statement.parameters[0].name, "orm_p0");
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 5);
}

TEST_F(DefaultSelectCommandTest, selectWithLogicalPredicates)
{
    orm::Query<models::ModelWithFloat> query;

    query.where((col("field1") >= 2 && col("field2").like("%abc%")) || !col("field3").isNull());

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql,
              "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
              "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
              "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
              "FROM models_ModelWithFloat WHERE ((models_ModelWithFloat.field1 >= :orm_p0 AND "
              "models_ModelWithFloat.field2 LIKE :orm_p1) OR (NOT (models_ModelWithFloat.field3 IS NULL)));");
    ASSERT_EQ(statement.parameters.size(), 2);
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 2);
    EXPECT_EQ(getValue<std::string>(statement.parameters[1]), "%abc%");
}

TEST_F(DefaultSelectCommandTest, selectWithInAndBetweenPredicates)
{
    orm::Query<models::ModelWithFloat> query;

    query.where(col("field1").in({1, 2, 3}) && col("field3").between(1.0, 3.5));

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql,
              "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
              "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
              "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
              "FROM models_ModelWithFloat WHERE (models_ModelWithFloat.field1 IN (:orm_p0, :orm_p1, :orm_p2) "
              "AND models_ModelWithFloat.field3 BETWEEN :orm_p3 AND :orm_p4);");
    ASSERT_EQ(statement.parameters.size(), 5);
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 1);
    EXPECT_EQ(getValue<int>(statement.parameters[1]), 2);
    EXPECT_EQ(getValue<int>(statement.parameters[2]), 3);
    EXPECT_EQ(getValue<double>(statement.parameters[3]), 1.0);
    EXPECT_EQ(getValue<double>(statement.parameters[4]), 3.5);
}

TEST_F(DefaultSelectCommandTest, selectWithOrderBy)
{
    orm::Query<models::ModelWithFloat> query;

    query.orderBy(desc(col("field2")), asc(col("field1"))).limit(10);

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
              "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
              "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
              "FROM models_ModelWithFloat ORDER BY models_ModelWithFloat.field2 DESC, "
              "models_ModelWithFloat.field1 ASC LIMIT 10;");
}

TEST_F(DefaultSelectCommandTest, selectWithRawPredicateAndRawOrder)
{
    orm::Query<models::ModelWithFloat> query;

    query.where(raw("LOWER(models_ModelWithFloat.field2) = :name", param("name", "wojtek")))
        .orderBy(rawOrder("LOWER(models_ModelWithFloat.field2) ASC"));

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql, "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
                             "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
                             "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
                             "FROM models_ModelWithFloat WHERE LOWER(models_ModelWithFloat.field2) = :name "
                             "ORDER BY LOWER(models_ModelWithFloat.field2) ASC;");
    ASSERT_EQ(statement.parameters.size(), 1);
    EXPECT_EQ(statement.parameters[0].name, "name");
    EXPECT_EQ(getValue<std::string>(statement.parameters[0]), "wojtek");
}

TEST_F(DefaultSelectCommandTest, selectWithMappedFieldName)
{
    orm::Query<models::ModelWithIdAndNamesMapping> query;

    query.where(col("field1") == 7);

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              "SELECT models_ModelWithIdAndNamesMapping.some_id_name AS "
              "models_ModelWithIdAndNamesMapping_some_id_name, "
              "models_ModelWithIdAndNamesMapping.some_field1_name AS "
              "models_ModelWithIdAndNamesMapping_some_field1_name, "
              "models_ModelWithIdAndNamesMapping.some_field2_name AS "
              "models_ModelWithIdAndNamesMapping_some_field2_name "
              "FROM models_ModelWithIdAndNamesMapping WHERE "
              "models_ModelWithIdAndNamesMapping.some_field1_name = :orm_p0;");
}

TEST_F(DefaultSelectCommandTest, selectWithRelatedFieldPath)
{
    orm::Query<models::ModelRelatedToOtherModel> query;

    query.where(col("field3.field2") == "test");

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              "SELECT models_ModelRelatedToOtherModel.id AS models_ModelRelatedToOtherModel_id, "
              "models_ModelRelatedToOtherModel.field1 AS models_ModelRelatedToOtherModel_field1, "
              "models_ModelRelatedToOtherModel.field2 AS models_ModelRelatedToOtherModel_field2, "
              "field3.id AS field3_id, field3.field1 AS field3_field1, field3.field2 AS field3_field2 "
              "FROM models_ModelRelatedToOtherModel "
              "LEFT JOIN models_ModelWithId AS field3 ON field3.id = models_ModelRelatedToOtherModel.field3_id "
              "WHERE field3.field2 = :orm_p0;");
}

TEST_F(DefaultSelectCommandTest, selectWithModelRelatedToOtherModelWithoutJoining)
{
    orm::Query<models::ModelRelatedToOtherModel> query;

    query.disableJoining();

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              selectSqlWithModelRelatedToOtherModelWithoutJoining);
}

TEST_F(DefaultSelectCommandTest, selectWithModelRelatedToOtherModelWithJoining)
{
    orm::Query<models::ModelRelatedToOtherModel> query;

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql, selectSqlWithModelRelatedToOtherModelWithJoining);
}

TEST_F(DefaultSelectCommandTest, selectWithModelRelatedToCompositeIdModelWithJoining)
{
    orm::Query<models::ModelRelatedToCompositeIdModel> query;

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              selectSqlWithModelRelatedToCompositeIdModelWithJoining);
}

TEST_F(DefaultSelectCommandTest, selectWithUnknownColumn_shouldThrow)
{
    orm::Query<models::ModelWithFloat> query;

    query.where(col("missing") == 1);

    EXPECT_THROW(command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectWithReservedRawParameter_shouldThrow)
{
    orm::Query<models::ModelWithFloat> query;

    query.where(raw("field1 = :orm_p0", param("orm_p0", 1)));

    EXPECT_THROW(command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectWithDuplicateRawParameter_shouldThrow)
{
    orm::Query<models::ModelWithFloat> query;

    query.where(raw("field1 = :value OR field2 = :value", param("value", 1), param("value", 2)));

    EXPECT_THROW(command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}
