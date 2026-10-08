#include <gtest/gtest.h>
#include <string>
#include <variant>

#include "tests/compile_fail/StaticPlanModels.hpp"
#include "tests/utils/FakeDatabase.hpp"

using namespace orm::query;
using namespace static_plan_models;
namespace ast = orm::query::detail;

TEST(StaticPlanCoverageTest, runtimeConstructionKeepsEverySelectClauseAndUniqueIncludes)
{
    const int minimum = 18;
    const auto base = select<User>().where(col<&User::age>() >= minimum);
    const auto plan = base.orderBy(desc(col<&User::id>()))
                          .groupBy(col<&User::age>())
                          .having(countAll<User>() >= 1)
                          .andHaving(sum(col<&User::age>()) > minimum)
                          .orHaving(min(col<&User::age>()) >= minimum)
                          .distinct()
                          .disableJoining()
                          .limit(param<std::size_t, 0>())
                          .offset(param<std::size_t, 1>())
                          .include<&User::roles>()
                          .include<&User::roles>();
    auto original = base.toDynamic();
    auto dynamic = plan.toDynamic(std::size_t{2}, std::size_t{1});
    const auto& untouched = orm::FakeDatabase::getSelectSpec(original);
    const auto& spec = orm::FakeDatabase::getSelectSpec(dynamic);
    EXPECT_TRUE(untouched.orderBy.empty());
    EXPECT_TRUE(untouched.includes.empty());
    ASSERT_EQ(spec.orderBy.size(), 1);
    EXPECT_EQ(spec.orderBy.front().direction, ast::OrderDirection::Desc);
    ASSERT_EQ(spec.groupBy.size(), 1);
    EXPECT_EQ(spec.groupBy.front().getPath(), "age");
    ASSERT_TRUE(spec.having.has_value());
    EXPECT_EQ(std::get<ast::AggregateLogicalExpression>(spec.having->getNode().expression).logicalOperator,
              ast::LogicalOperator::Or);
    EXPECT_TRUE(spec.isDistinct);
    EXPECT_FALSE(spec.shouldJoin);
    EXPECT_EQ(spec.limit, 2);
    EXPECT_EQ(spec.offset, 1);
    ASSERT_EQ(spec.includes.size(), 1);
    EXPECT_EQ(spec.includes.front(), "roles");
}

TEST(StaticPlanCoverageTest, runtimeProjectionReplacementAndInitialHavingKeepTheirClauses)
{
    const std::string oldAlias{"previous"};
    const auto initial = selectAs<User, UserName>(as(oldAlias, col<&User::name>()));
    const auto plan = initial.project(as<"name">(col<&User::name>()))
                          .groupBy(col<&User::name>())
                          .andHaving(countAll<User>() > 0)
                          .orderBy(asc(col<&User::name>()))
                          .distinct()
                          .disableJoining()
                          .limit(std::size_t{3})
                          .offset(std::size_t{2});
    auto dynamic = plan.toDynamic();
    const auto& spec = orm::FakeDatabase::getSelectSpec(dynamic);
    ASSERT_EQ(spec.projections.size(), 1);
    EXPECT_EQ(spec.projections.front().resultField, "name");
    ASSERT_TRUE(spec.having.has_value());
    EXPECT_TRUE(std::holds_alternative<ast::AggregateComparisonExpression>(spec.having->getNode().expression));
    EXPECT_TRUE(spec.isDistinct);
    EXPECT_FALSE(spec.shouldJoin);
    EXPECT_EQ(spec.limit, 3);
    EXPECT_EQ(spec.offset, 2);

    auto firstOr = select<User>().orHaving(countAll<User>() > 0).toDynamic();
    EXPECT_TRUE(orm::FakeDatabase::getSelectSpec(firstOr).having.has_value());
}

TEST(StaticPlanCoverageTest, runtimeRemoveConstructionProducesTheBoundPredicate)
{
    const int id = 4;
    const auto plan = remove<User>().where(col<&User::id>() == id);
    const auto predicate = ast::erase(plan.toDynamic());
    const auto& comparison = std::get<ast::ComparisonExpression>(predicate.getNode().expression);
    EXPECT_EQ(comparison.column.getPath(), "id");
    EXPECT_EQ(std::get<int>(comparison.value.get()), id);
}
