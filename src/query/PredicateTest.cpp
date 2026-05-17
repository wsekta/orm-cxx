#include "orm-cxx/query/Predicate.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

using namespace orm::query;

TEST(PredicateTest, shouldCreateComparisonPredicates)
{
    EXPECT_NO_THROW(static_cast<void>(col("field1") == 1));
    EXPECT_NO_THROW(static_cast<void>(col("field1") != 1));
    EXPECT_NO_THROW(static_cast<void>(col("field1") > 1));
    EXPECT_NO_THROW(static_cast<void>(col("field1") >= 1));
    EXPECT_NO_THROW(static_cast<void>(col("field1") < 1));
    EXPECT_NO_THROW(static_cast<void>(col("field1") <= 1));
    EXPECT_NO_THROW(static_cast<void>(col("field2").like("%value%")));
    EXPECT_NO_THROW(static_cast<void>(col("field2").notLike("%value%")));
}

TEST(PredicateTest, shouldCreateNullPredicates)
{
    EXPECT_NO_THROW(static_cast<void>(col("field1").isNull()));
    EXPECT_NO_THROW(static_cast<void>(col("field1").isNotNull()));
    EXPECT_NO_THROW(static_cast<void>(col("field1") == nullptr));
    EXPECT_NO_THROW(static_cast<void>(col("field1") != nullptr));
}

TEST(PredicateTest, shouldCreateListAndBetweenPredicates)
{
    EXPECT_NO_THROW(static_cast<void>(col("field1").in({1, 2, 3})));
    EXPECT_NO_THROW(static_cast<void>(col("field1").notIn({1, 2, 3})));
    EXPECT_NO_THROW(static_cast<void>(col("field1").between(1, 3)));
    EXPECT_NO_THROW(static_cast<void>(col("field1").notBetween(1, 3)));
}

TEST(PredicateTest, shouldThrowForEmptyInPredicate)
{
    EXPECT_THROW(static_cast<void>(col("field1").in(std::vector<int>{})), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(col("field1").notIn(std::vector<int>{})), std::invalid_argument);
}

TEST(PredicateTest, shouldCreateLogicalPredicates)
{
    EXPECT_NO_THROW(static_cast<void>((col("field1") == 1 && col("field2").isNotNull()) || !col("field3").isNull()));
}

TEST(PredicateTest, shouldCreateTypedFieldHelper)
{
    EXPECT_NO_THROW(static_cast<void>(field<void, int>("field1") == 1));
}

TEST(PredicateTest, shouldCreateRawPredicate)
{
    EXPECT_NO_THROW(static_cast<void>(raw("LOWER(name) = :name", param("name", "wojtek"))));
}
