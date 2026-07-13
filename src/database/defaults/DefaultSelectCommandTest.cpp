#include "DefaultSelectCommand.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <variant>

#include "orm-cxx/projection_query.hpp"
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
auto getValue(const orm::db::StatementParameter& parameter) -> T
{
    return std::get<T>(parameter.value->get());
}

auto expectedModelWithFloatWhereSql(const std::string& predicateSql) -> std::string
{
    return "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
           "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
           "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
           "FROM models_ModelWithFloat WHERE " +
           predicateSql + ";";
}

auto renderModelWithFloatWhereSql(orm::db::commands::DefaultSelectCommand& command,
                                  const orm::query::Predicate& predicate) -> std::string
{
    orm::Query<models::ModelWithFloat> query;
    query.where(predicate);

    return command.select(orm::Database::getQueryData(query)).sql;
}

} // namespace

namespace projection_select_command_test_models
{
struct ModelWithIdProjection
{
    int id;
    std::string name;
};

struct RelatedProjection
{
    int id;
    int relatedId;
};

struct AggregateFunctionsProjection
{
    long long allRows;
    long long countedNames;
    long long totalField1;
    double averageField1;
    int minField1;
    int maxField1;
};

struct GroupedAggregateProjection
{
    std::string name;
    long long users;
    double averageField1;
};

struct RelatedAggregateProjection
{
    int relatedId;
    long long users;
};

struct RelatedNameAggregateProjection
{
    std::string relatedName;
    long long users;
};
} // namespace projection_select_command_test_models

using namespace projection_select_command_test_models;

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

TEST_F(DefaultSelectCommandTest, selectFullModelWithGroupByHavingAndClauseOrder)
{
    orm::Query<models::ModelWithId> query;

    query.where(col("field1") >= 10)
        .groupBy(col("field2"))
        .having(countAll() > 1)
        .andHaving(avg(col("field1")) >= 10.0)
        .orderBy(desc(col("field2")))
        .limit(5)
        .offset(2);

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql, "SELECT models_ModelWithId.id AS models_ModelWithId_id, "
                             "models_ModelWithId.field1 AS models_ModelWithId_field1, "
                             "models_ModelWithId.field2 AS models_ModelWithId_field2 FROM models_ModelWithId "
                             "WHERE models_ModelWithId.field1 >= :orm_p0 GROUP BY models_ModelWithId.field2 "
                             "HAVING (COUNT(*) > :orm_p1 AND AVG(models_ModelWithId.field1) >= :orm_p2) "
                             "ORDER BY models_ModelWithId.field2 DESC LIMIT 5 OFFSET 2;");
    ASSERT_EQ(statement.parameters.size(), 3);
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 10);
    EXPECT_EQ(getValue<int>(statement.parameters[1]), 1);
    EXPECT_EQ(getValue<double>(statement.parameters[2]), 10.0);
}

TEST_F(DefaultSelectCommandTest, selectFullModelWithAllAggregateFunctionsInHaving)
{
    orm::Query<models::ModelWithId> query;

    query.groupBy(col("field2"))
        .having((count(col("field2")) == 2) && (sum(col("field1")) != 30) && (min(col("id")) < 5) &&
                (max(col("id")) <= 4));

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql,
              "SELECT models_ModelWithId.id AS models_ModelWithId_id, "
              "models_ModelWithId.field1 AS models_ModelWithId_field1, "
              "models_ModelWithId.field2 AS models_ModelWithId_field2 FROM models_ModelWithId "
              "GROUP BY models_ModelWithId.field2 HAVING (((COUNT(models_ModelWithId.field2) = :orm_p0 AND "
              "SUM(models_ModelWithId.field1) != :orm_p1) AND MIN(models_ModelWithId.id) < :orm_p2) AND "
              "MAX(models_ModelWithId.id) <= :orm_p3);");
    ASSERT_EQ(statement.parameters.size(), 4);
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 2);
    EXPECT_EQ(getValue<int>(statement.parameters[1]), 30);
    EXPECT_EQ(getValue<int>(statement.parameters[2]), 5);
    EXPECT_EQ(getValue<int>(statement.parameters[3]), 4);
}

TEST_F(DefaultSelectCommandTest, selectFullModelWithMappedGroupByAndHavingColumns)
{
    orm::Query<models::ModelWithIdAndNamesMapping> query;

    query.groupBy(col("field2")).having(count(col("field1")) > 1);

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(
        statement.sql,
        "SELECT models_ModelWithIdAndNamesMapping.some_id_name AS models_ModelWithIdAndNamesMapping_some_id_name, "
        "models_ModelWithIdAndNamesMapping.some_field1_name AS "
        "models_ModelWithIdAndNamesMapping_some_field1_name, "
        "models_ModelWithIdAndNamesMapping.some_field2_name AS "
        "models_ModelWithIdAndNamesMapping_some_field2_name FROM models_ModelWithIdAndNamesMapping "
        "GROUP BY models_ModelWithIdAndNamesMapping.some_field2_name "
        "HAVING COUNT(models_ModelWithIdAndNamesMapping.some_field1_name) > :orm_p0;");
    ASSERT_EQ(statement.parameters.size(), 1);
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 1);
}

TEST_F(DefaultSelectCommandTest, selectFullModelWithRelatedGroupByAndHavingPath)
{
    orm::Query<models::ModelRelatedToOtherModel> query;

    query.groupBy(col("field3.field2")).having(count(col("field3.id")) > 1);

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql,
              "SELECT models_ModelRelatedToOtherModel.id AS models_ModelRelatedToOtherModel_id, "
              "models_ModelRelatedToOtherModel.field1 AS models_ModelRelatedToOtherModel_field1, "
              "models_ModelRelatedToOtherModel.field2 AS models_ModelRelatedToOtherModel_field2, "
              "field3.id AS field3_id, field3.field1 AS field3_field1, field3.field2 AS field3_field2 "
              "FROM models_ModelRelatedToOtherModel "
              "LEFT JOIN models_ModelWithId AS field3 ON field3.id = models_ModelRelatedToOtherModel.field3_id "
              "GROUP BY field3.field2 HAVING COUNT(field3.id) > :orm_p0;");
    ASSERT_EQ(statement.parameters.size(), 1);
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 1);
}

TEST_F(DefaultSelectCommandTest, selectFullModelWithRelatedPrimaryKeyGroupingWithoutJoining)
{
    orm::Query<models::ModelRelatedToOtherModel> query;

    query.groupBy(col("field3.id")).having(count(col("field3.id")) > 1).disableJoining();

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql, "SELECT models_ModelRelatedToOtherModel.id AS models_ModelRelatedToOtherModel_id, "
                             "models_ModelRelatedToOtherModel.field1 AS models_ModelRelatedToOtherModel_field1, "
                             "models_ModelRelatedToOtherModel.field2 AS models_ModelRelatedToOtherModel_field2, "
                             "models_ModelRelatedToOtherModel.field3_id AS models_ModelRelatedToOtherModel_field3_id "
                             "FROM models_ModelRelatedToOtherModel GROUP BY models_ModelRelatedToOtherModel.field3_id "
                             "HAVING COUNT(models_ModelRelatedToOtherModel.field3_id) > :orm_p0;");
    ASSERT_EQ(statement.parameters.size(), 1);
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 1);
}

TEST_F(DefaultSelectCommandTest, selectFullModelWithRelatedNonPrimaryKeyGroupingWithoutJoining_shouldThrow)
{
    orm::Query<models::ModelRelatedToOtherModel> query;

    query.groupBy(col("field3.field2")).having(count(col("field3.field1")) > 1).disableJoining();

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectProjection)
{
    orm::ProjectionQuery<models::ModelWithId, ModelWithIdProjection> query;

    query.project(as("id", col("id")), as("name", col("field2")));

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              "SELECT models_ModelWithId.id AS id, models_ModelWithId.field2 AS name FROM models_ModelWithId;");
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithMappedFieldName)
{
    orm::ProjectionQuery<models::ModelWithIdAndNamesMapping, ModelWithIdProjection> query;

    query.project(as("id", col("id")), as("name", col("field2")));

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              "SELECT models_ModelWithIdAndNamesMapping.some_id_name AS id, "
              "models_ModelWithIdAndNamesMapping.some_field2_name AS name "
              "FROM models_ModelWithIdAndNamesMapping;");
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithRelatedFieldPath)
{
    orm::ProjectionQuery<models::ModelRelatedToOtherModel, ModelWithIdProjection> query;

    query.project(as("id", col("id")), as("name", col("field3.field2")));

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              "SELECT models_ModelRelatedToOtherModel.id AS id, field3.field2 AS name "
              "FROM models_ModelRelatedToOtherModel "
              "LEFT JOIN models_ModelWithId AS field3 ON field3.id = models_ModelRelatedToOtherModel.field3_id;");
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithRelatedPrimaryKeyWithoutJoining)
{
    orm::ProjectionQuery<models::ModelRelatedToOtherModel, RelatedProjection> query;

    query.project(as("id", col("id")), as("relatedId", col("field3.id"))).disableJoining();

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              "SELECT models_ModelRelatedToOtherModel.id AS id, "
              "models_ModelRelatedToOtherModel.field3_id AS relatedId FROM models_ModelRelatedToOtherModel;");
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithRelatedNonPrimaryKeyWithoutJoining_shouldThrow)
{
    orm::ProjectionQuery<models::ModelRelatedToOtherModel, ModelWithIdProjection> query;

    query.project(as("id", col("id")), as("name", col("field3.field2"))).disableJoining();

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithWhereOrderDistinctLimitOffset)
{
    orm::ProjectionQuery<models::ModelWithId, ModelWithIdProjection> query;

    query.project(as("id", col("id")), as("name", col("field2")))
        .distinct()
        .where(col("field1") >= 10)
        .orderBy(desc(col("id")))
        .limit(1)
        .offset(2);

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql, "SELECT DISTINCT models_ModelWithId.id AS id, models_ModelWithId.field2 AS name "
                             "FROM models_ModelWithId WHERE models_ModelWithId.field1 >= :orm_p0 "
                             "ORDER BY models_ModelWithId.id DESC LIMIT 1 OFFSET 2;");
    ASSERT_EQ(statement.parameters.size(), 1);
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 10);
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithAggregateFunctions)
{
    orm::ProjectionQuery<models::ModelWithId, AggregateFunctionsProjection> query;

    query.project(as("allRows", countAll()), as("countedNames", count(col("field2"))),
                  as("totalField1", sum(col("field1"))), as("averageField1", avg(col("field1"))),
                  as("minField1", min(col("field1"))), as("maxField1", max(col("field1"))));

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              "SELECT COUNT(*) AS allRows, COUNT(models_ModelWithId.field2) AS countedNames, "
              "SUM(models_ModelWithId.field1) AS totalField1, AVG(models_ModelWithId.field1) AS averageField1, "
              "MIN(models_ModelWithId.field1) AS minField1, MAX(models_ModelWithId.field1) AS maxField1 "
              "FROM models_ModelWithId;");
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithAggregateMappedFieldName)
{
    orm::ProjectionQuery<models::ModelWithIdAndNamesMapping, AggregateFunctionsProjection> query;

    query.project(as("allRows", countAll()), as("countedNames", count(col("field2"))),
                  as("totalField1", sum(col("field1"))), as("averageField1", avg(col("field1"))),
                  as("minField1", min(col("field1"))), as("maxField1", max(col("field1"))));

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              "SELECT COUNT(*) AS allRows, COUNT(models_ModelWithIdAndNamesMapping.some_field2_name) AS countedNames, "
              "SUM(models_ModelWithIdAndNamesMapping.some_field1_name) AS totalField1, "
              "AVG(models_ModelWithIdAndNamesMapping.some_field1_name) AS averageField1, "
              "MIN(models_ModelWithIdAndNamesMapping.some_field1_name) AS minField1, "
              "MAX(models_ModelWithIdAndNamesMapping.some_field1_name) AS maxField1 "
              "FROM models_ModelWithIdAndNamesMapping;");
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithGroupByHavingAndClauseOrder)
{
    orm::ProjectionQuery<models::ModelWithId, GroupedAggregateProjection> query;

    query.project(as("name", col("field2")), as("users", countAll()), as("averageField1", avg(col("field1"))))
        .where(col("field1") >= 10)
        .groupBy(col("field2"))
        .having(countAll() > 1)
        .andHaving(avg(col("field1")) >= 10.0)
        .orderBy(desc(col("field2")))
        .limit(5)
        .offset(2);

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql, "SELECT models_ModelWithId.field2 AS name, COUNT(*) AS users, "
                             "AVG(models_ModelWithId.field1) AS averageField1 FROM models_ModelWithId "
                             "WHERE models_ModelWithId.field1 >= :orm_p0 GROUP BY models_ModelWithId.field2 "
                             "HAVING (COUNT(*) > :orm_p1 AND AVG(models_ModelWithId.field1) >= :orm_p2) "
                             "ORDER BY models_ModelWithId.field2 DESC LIMIT 5 OFFSET 2;");
    ASSERT_EQ(statement.parameters.size(), 3);
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 10);
    EXPECT_EQ(getValue<int>(statement.parameters[1]), 1);
    EXPECT_EQ(getValue<double>(statement.parameters[2]), 10.0);
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithOrAndNotHaving)
{
    orm::ProjectionQuery<models::ModelWithId, GroupedAggregateProjection> query;

    query.project(as("name", col("field2")), as("users", countAll()), as("averageField1", avg(col("field1"))))
        .groupBy(col("field2"))
        .having(countAll() > 1)
        .orHaving(!(max(col("id")) <= 3));

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql, "SELECT models_ModelWithId.field2 AS name, COUNT(*) AS users, "
                             "AVG(models_ModelWithId.field1) AS averageField1 FROM models_ModelWithId "
                             "GROUP BY models_ModelWithId.field2 HAVING (COUNT(*) > :orm_p0 OR "
                             "(NOT (MAX(models_ModelWithId.id) <= :orm_p1)));");
    ASSERT_EQ(statement.parameters.size(), 2);
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 1);
    EXPECT_EQ(getValue<int>(statement.parameters[1]), 3);
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithRemainingHavingComparisonOperators)
{
    orm::ProjectionQuery<models::ModelWithId, GroupedAggregateProjection> query;

    query.project(as("name", col("field2")), as("users", countAll()), as("averageField1", avg(col("field1"))))
        .groupBy(col("field2"))
        .having((countAll() == 2) && (sum(col("field1")) != 30) && (min(col("id")) < 5));

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql, "SELECT models_ModelWithId.field2 AS name, COUNT(*) AS users, "
                             "AVG(models_ModelWithId.field1) AS averageField1 FROM models_ModelWithId "
                             "GROUP BY models_ModelWithId.field2 HAVING ((COUNT(*) = :orm_p0 AND "
                             "SUM(models_ModelWithId.field1) != :orm_p1) AND MIN(models_ModelWithId.id) < :orm_p2);");
    ASSERT_EQ(statement.parameters.size(), 3);
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 2);
    EXPECT_EQ(getValue<int>(statement.parameters[1]), 30);
    EXPECT_EQ(getValue<int>(statement.parameters[2]), 5);
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithRelatedGroupByPath)
{
    orm::ProjectionQuery<models::ModelRelatedToOtherModel, RelatedNameAggregateProjection> query;

    query.project(as("relatedName", col("field3.field2")), as("users", countAll())).groupBy(col("field3.field2"));

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              "SELECT field3.field2 AS relatedName, COUNT(*) AS users FROM models_ModelRelatedToOtherModel "
              "LEFT JOIN models_ModelWithId AS field3 ON field3.id = models_ModelRelatedToOtherModel.field3_id "
              "GROUP BY field3.field2;");
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithRelatedPrimaryKeyGroupByWithoutJoining)
{
    orm::ProjectionQuery<models::ModelRelatedToOtherModel, RelatedAggregateProjection> query;

    query.project(as("relatedId", col("field3.id")), as("users", countAll()))
        .groupBy(col("field3.id"))
        .disableJoining();

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)).sql,
              "SELECT models_ModelRelatedToOtherModel.field3_id AS relatedId, COUNT(*) AS users "
              "FROM models_ModelRelatedToOtherModel GROUP BY models_ModelRelatedToOtherModel.field3_id;");
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithRelatedNonPrimaryKeyGroupByWithoutJoining_shouldThrow)
{
    orm::ProjectionQuery<models::ModelRelatedToOtherModel, RelatedAggregateProjection> query;

    query.project(as("relatedId", col("field3.id")), as("users", countAll()))
        .groupBy(col("field3.field2"))
        .disableJoining();

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithInvalidAggregateSource_shouldThrow)
{
    orm::ProjectionQuery<models::ModelWithId, ModelWithIdProjection> query;

    query.project(as("id", col("id")), as("name", count(col("missing"))));

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithAggregateWithoutSourceColumn_shouldThrow)
{
    orm::ProjectionQuery<models::ModelWithId, ModelWithIdProjection> query;

    query.project(as("id", col("id")),
                  as("name", AggregateExpression{.function = AggregateFunction::Sum, .column = std::nullopt}));

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithUnsupportedAggregateFunction_shouldThrow)
{
    orm::ProjectionQuery<models::ModelWithId, ModelWithIdProjection> query;

    query.project(as("id", col("id")), as("name", AggregateExpression{.function = static_cast<AggregateFunction>(999),
                                                                      .column = col("field1")}));

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithUnsupportedHavingOperator_shouldThrow)
{
    orm::ProjectionQuery<models::ModelWithId, GroupedAggregateProjection> query;
    const auto having = AggregatePredicate{AggregatePredicateNode{AggregateComparisonExpression{
        .aggregate = countAll(), .comparisonOperator = ComparisonOperator::Like, .value = QueryValue{1}}}};

    query.project(as("name", col("field2")), as("users", countAll()), as("averageField1", avg(col("field1"))))
        .groupBy(col("field2"))
        .having(having);

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectProjectionWithUnknownSourceColumn_shouldThrow)
{
    orm::ProjectionQuery<models::ModelWithId, ModelWithIdProjection> query;

    query.project(as("id", col("id")), as("name", col("missing")));

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
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

TEST_F(DefaultSelectCommandTest, selectWithComparisonOperators_shouldRenderSqlOperators)
{
    EXPECT_EQ(renderModelWithFloatWhereSql(command, col("field1") != 5),
              expectedModelWithFloatWhereSql("models_ModelWithFloat.field1 != :orm_p0"));
    EXPECT_EQ(renderModelWithFloatWhereSql(command, col("field1") > 5),
              expectedModelWithFloatWhereSql("models_ModelWithFloat.field1 > :orm_p0"));
    EXPECT_EQ(renderModelWithFloatWhereSql(command, col("field1") >= 5),
              expectedModelWithFloatWhereSql("models_ModelWithFloat.field1 >= :orm_p0"));
    EXPECT_EQ(renderModelWithFloatWhereSql(command, col("field1") < 5),
              expectedModelWithFloatWhereSql("models_ModelWithFloat.field1 < :orm_p0"));
    EXPECT_EQ(renderModelWithFloatWhereSql(command, col("field1") <= 5),
              expectedModelWithFloatWhereSql("models_ModelWithFloat.field1 <= :orm_p0"));
    EXPECT_EQ(renderModelWithFloatWhereSql(command, col("field2").notLike("%abc%")),
              expectedModelWithFloatWhereSql("models_ModelWithFloat.field2 NOT LIKE :orm_p0"));
}

TEST_F(DefaultSelectCommandTest, selectWithUnsupportedComparisonOperator_shouldThrow)
{
    orm::Query<models::ModelWithFloat> query;
    query.where(orm::query::Predicate{orm::query::PredicateNode{orm::query::ComparisonExpression{
        .column = col("field1"),
        .comparisonOperator = static_cast<orm::query::ComparisonOperator>(999),
        .value = orm::query::QueryValue{1},
    }}});

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
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

TEST_F(DefaultSelectCommandTest, selectWithEmptyColumnPath_shouldThrow)
{
    orm::Query<models::ModelWithFloat> query;

    query.where(col("") == 1);

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectWithScalarUsedAsRelatedPath_shouldThrow)
{
    orm::Query<models::ModelWithFloat> query;

    query.where(col("field1.field2") == 1);

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectWithRelatedModelColumn_shouldThrow)
{
    orm::Query<models::ModelRelatedToOtherModel> query;

    query.where(col("field3") == 1);

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectWithRelatedNonPrimaryKeyWithoutJoining_shouldThrow)
{
    orm::Query<models::ModelRelatedToOtherModel> query;

    query.disableJoining().where(col("field3.field2") == "target");

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectWithRelatedPrimaryKeyWithoutJoining_shouldUseForeignKeyColumn)
{
    orm::Query<models::ModelRelatedToOtherModel> query;

    query.disableJoining().where(col("field3.id") == 2);

    const auto statement = command.select(orm::Database::getQueryData(query));

    EXPECT_EQ(statement.sql,
              "SELECT models_ModelRelatedToOtherModel.id AS models_ModelRelatedToOtherModel_id, "
              "models_ModelRelatedToOtherModel.field1 AS models_ModelRelatedToOtherModel_field1, "
              "models_ModelRelatedToOtherModel.field2 AS models_ModelRelatedToOtherModel_field2, "
              "models_ModelRelatedToOtherModel.field3_id AS models_ModelRelatedToOtherModel_field3_id "
              "FROM models_ModelRelatedToOtherModel WHERE models_ModelRelatedToOtherModel.field3_id = :orm_p0;");
    ASSERT_EQ(statement.parameters.size(), 1);
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 2);
}

TEST_F(DefaultSelectCommandTest, selectWithTooDeepRelatedPath_shouldThrow)
{
    orm::Query<models::ModelRelatedToOtherModel> query;

    query.where(col("field3.id.extra") == 1);

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
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

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectWithReservedRawParameter_shouldThrow)
{
    orm::Query<models::ModelWithFloat> query;

    query.where(raw("field1 = :orm_p0", param("orm_p0", 1)));

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectWithDuplicateRawParameter_shouldThrow)
{
    orm::Query<models::ModelWithFloat> query;

    query.where(raw("field1 = :value OR field2 = :value", param("value", 1), param("value", 2)));

    EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
}

TEST_F(DefaultSelectCommandTest, selectWithInvalidRawParameterName_shouldThrow)
{
    {
        orm::Query<models::ModelWithFloat> query;
        query.where(raw("field1 = :value", param("", 1)));

        EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
    }

    {
        orm::Query<models::ModelWithFloat> query;
        query.where(raw("field1 = :value", param(":value", 1)));

        EXPECT_THROW((void)command.select(orm::Database::getQueryData(query)), std::invalid_argument);
    }
}
