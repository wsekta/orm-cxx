#include <gtest/gtest.h>
#include <variant>

#include "orm-cxx/query.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/FakeDatabase.hpp"

using namespace orm::query;

TEST(QueryTest, shouldStoreGroupByAndCombinedHavingPredicates)
{
    orm::Query<models::ModelWithId> query;

    query.groupBy(col("field2"), col("field1"))
        .having(countAll() > 1)
        .andHaving(avg(col("field1")) >= 10.0)
        .orHaving(!(max(col("id")) <= 3));

    const auto& data = orm::Database::getQueryData(query);

    ASSERT_EQ(data.groupBy.size(), 2);
    EXPECT_EQ(data.groupBy[0].getPath(), "field2");
    EXPECT_EQ(data.groupBy[1].getPath(), "field1");
    ASSERT_TRUE(data.having.has_value());

    const auto& havingRoot = data.having->getNode();
    ASSERT_TRUE(std::holds_alternative<AggregateLogicalExpression>(havingRoot.expression));
    EXPECT_EQ(std::get<AggregateLogicalExpression>(havingRoot.expression).logicalOperator, LogicalOperator::Or);
}

TEST(QueryTest, shouldInitializeHavingWithAndHavingOrOrHaving)
{
    orm::Query<models::ModelWithId> andQuery;
    andQuery.andHaving(count(col("id")) > 0);

    const auto& andData = orm::Database::getQueryData(andQuery);
    ASSERT_TRUE(andData.having.has_value());
    EXPECT_TRUE(std::holds_alternative<AggregateComparisonExpression>(andData.having->getNode().expression));

    orm::Query<models::ModelWithId> orQuery;
    orQuery.orHaving(sum(col("field1")) > 0);

    const auto& orData = orm::Database::getQueryData(orQuery);
    ASSERT_TRUE(orData.having.has_value());
    EXPECT_TRUE(std::holds_alternative<AggregateComparisonExpression>(orData.having->getNode().expression));
}
