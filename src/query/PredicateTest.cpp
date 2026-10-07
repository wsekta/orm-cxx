#include "orm-cxx/query/Predicate.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

#include "tests/ModelsDefinitions.hpp"

using namespace orm::query;

TEST(PredicateTest, shouldCreateComparisonPredicates)
{
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>() == 1));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>() != 1));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>() > 1));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>() >= 1));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>() < 1));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>() <= 1));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field2>().like("%value%")));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field2>().notLike("%value%")));
}

TEST(PredicateTest, shouldCreateNullPredicates)
{
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>().isNull()));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>().isNotNull()));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>() == nullptr));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>() != nullptr));
}

TEST(PredicateTest, internalNullComparisonsPreserveColumnAndOperator)
{
    const detail::Column column{"field1"};
    const auto isNull = column == nullptr;
    const auto isNotNull = column != nullptr;
    const auto& nullExpression = std::get<detail::NullExpression>(isNull.getNode().expression);
    const auto& notNullExpression = std::get<detail::NullExpression>(isNotNull.getNode().expression);

    EXPECT_EQ(nullExpression.column.getPath(), "field1");
    EXPECT_EQ(nullExpression.nullOperator, detail::NullOperator::IsNull);
    EXPECT_EQ(notNullExpression.column.getPath(), "field1");
    EXPECT_EQ(notNullExpression.nullOperator, detail::NullOperator::IsNotNull);
}

TEST(PredicateTest, shouldCreateListAndBetweenPredicates)
{
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>().in({1, 2, 3})));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>().notIn({1, 2, 3})));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>().between(1, 3)));
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>().notBetween(1, 3)));
}

TEST(PredicateTest, shouldThrowForEmptyInPredicate)
{
    EXPECT_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>().in(std::vector<int>{})),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>().notIn(std::vector<int>{})),
                 std::invalid_argument);
}

TEST(PredicateTest, internalListsPreserveBoundValuesAndRejectEmptyInput)
{
    const detail::Column column{"field1"};
    const auto values = std::vector<QueryValue>{QueryValue{1}, QueryValue{2}};
    const auto excluded = column.notIn(values);
    const auto& expression = std::get<detail::ListExpression>(excluded.getNode().expression);
    EXPECT_EQ(expression.column.getPath(), "field1");
    EXPECT_EQ(expression.listOperator, detail::ListOperator::NotIn);
    EXPECT_EQ(expression.values, values);

    EXPECT_THROW((void)column.in(std::vector<int>{}), std::invalid_argument);
    EXPECT_THROW((void)column.notIn(std::vector<int>{}), std::invalid_argument);
    EXPECT_THROW((void)column.in(std::vector<QueryValue>{}), std::invalid_argument);
    EXPECT_THROW((void)column.notIn(std::vector<QueryValue>{}), std::invalid_argument);
}

TEST(PredicateTest, shouldCreateLogicalPredicates)
{
    EXPECT_NO_THROW(static_cast<void>(
        (col<&models::ModelWithOptional::field1>() == 1 && col<&models::ModelWithOptional::field2>().isNotNull()) ||
        !col<&models::ModelWithOptional::field3>().isNull()));
}

TEST(PredicateTest, shouldCreateTypedFieldReference)
{
    EXPECT_NO_THROW(static_cast<void>(col<&models::ModelWithOptional::field1>() == 1));
}

TEST(PredicateTest, shouldCreateRawPredicate)
{
    EXPECT_NO_THROW(static_cast<void>(raw<models::ModelWithOptional>("LOWER(name) = :name", param("name", "wojtek"))));
}
