#include "orm-cxx/query/Condition.hpp"

#include <gtest/gtest.h>

namespace
{

}

class ConditionTest : public ::testing::Test
{
};

TEST_F(ConditionTest, shouldCreateCondition)
{
    orm::query::Condition condition{"test"};

    EXPECT_EQ(condition.getColumnName(), "test");
    EXPECT_EQ(condition.getOperatorType(), orm::query::Operator::NO_OPERATOR);
}

TEST_F(ConditionTest, shouldSetLikeCondition)
{
    orm::query::Condition condition{"test"};
    condition.like("value");

    EXPECT_EQ(condition.getColumnName(), "test");
    EXPECT_EQ(condition.getOperatorType(), orm::query::Operator::LIKE);
    EXPECT_EQ(condition.getComparisonValue(), "value");
}

TEST_F(ConditionTest, shouldSetNotLikeCondition)
{
    orm::query::Condition condition{"test"};
    condition.notLike("value");

    EXPECT_EQ(condition.getColumnName(), "test");
    EXPECT_EQ(condition.getOperatorType(), orm::query::Operator::NOT_LIKE);
    EXPECT_EQ(condition.getComparisonValue(), "value");
}

TEST_F(ConditionTest, shouldSetIsNullCondition)
{
    orm::query::Condition condition{"test"};
    condition.isNull();

    EXPECT_EQ(condition.getColumnName(), "test");
    EXPECT_EQ(condition.getOperatorType(), orm::query::Operator::IS_NULL);
}

TEST_F(ConditionTest, shouldSetIsNotNullCondition)
{
    orm::query::Condition condition{"test"};
    condition.isNotNull();

    EXPECT_EQ(condition.getColumnName(), "test");
    EXPECT_EQ(condition.getOperatorType(), orm::query::Operator::IS_NOT_NULL);
}
