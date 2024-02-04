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
}

TEST_F(ConditionTest, shouldSetLikeCondition)
{
    orm::query::Condition condition{"test"};
    condition.like("value");
}

TEST_F(ConditionTest, shouldSetNotLikeCondition)
{
    orm::query::Condition condition{"test"};
    condition.notLike("value");
}

TEST_F(ConditionTest, shouldSetIsNullCondition)
{
    orm::query::Condition condition{"test"};
    condition.isNull();
}

TEST_F(ConditionTest, shouldSetIsNotNullCondition)
{
    orm::query::Condition condition{"test"};
    condition.isNotNull();
}
