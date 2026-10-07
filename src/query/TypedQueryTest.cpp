#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "orm-cxx/projection_query.hpp"
#include "tests/utils/FakeDatabase.hpp"

namespace typed_query_test_models
{
struct Profile
{
    int id;
    std::string city;
};

struct Role;

struct User
{
    int id;
    int age;
    long long wide;
    double score;
    float ratio;
    bool active;
    std::string name;
    std::optional<int> rank;
    std::optional<std::string> email;
    std::optional<Profile> profile;
    orm::ManyToMany<Role> roles;
};

struct Role
{
    int id;
    std::string name;
    orm::ManyToMany<User> users;
};

struct NameDto
{
    std::string name;
};
} // namespace typed_query_test_models

using namespace orm::query;
using namespace typed_query_test_models;
namespace ast = orm::query::detail;

static_assert(std::same_as<decltype(col<&User::age>())::Model, User>);
static_assert(std::same_as<decltype(col<&User::age>())::Value, int>);
static_assert(decltype(col<&User::profile, &Profile::id>())::nullable);
static_assert(not decltype(col<&User::age>())::nullable);
static_assert(ast::isSafeNumericWidening<long long, int>);
static_assert(ast::isSafeNumericWidening<double, float>);
static_assert(not ast::isSafeNumericWidening<int, unsigned int>);
static_assert(not ast::isSafeNumericWidening<float, int>);
static_assert(not ast::isSafeNumericWidening<float, double>);

namespace
{
auto comparison(const auto& typed) -> ast::ComparisonExpression
{
    return std::get<ast::ComparisonExpression>(ast::erase(typed).getNode().expression);
}

template <typename Source>
auto checkFloatingNormalization(Source value) -> void
{
    if constexpr (ast::isSafeNumericWidening<double, Source>)
    {
        const auto stored = comparison(col<&User::score>() == value).value;
        EXPECT_EQ(stored.getLogicalType(), orm::model::ColumnType::Double);
        EXPECT_DOUBLE_EQ(std::get<double>(stored.get()), static_cast<double>(value));
    }
    else
    {
        static_assert(not requires(Source candidate) { col<&User::score>() == candidate; });
    }
}
} // namespace

TEST(TypedQueryTest, resolvesMemberAndToOnePaths)
{
    EXPECT_EQ(ast::erase(col<&User::age>()).getPath(), "age");
    EXPECT_EQ((ast::erase(col<&User::profile, &Profile::city>()).getPath()), "profile.city");
}

TEST(TypedQueryTest, comparisonsPreserveOperatorsAndBoundValues)
{
    const auto age = col<&User::age>();
    EXPECT_EQ(comparison(age == 18).comparisonOperator, ast::ComparisonOperator::Equal);
    EXPECT_EQ(comparison(age != 18).comparisonOperator, ast::ComparisonOperator::NotEqual);
    EXPECT_EQ(comparison(age > 18).comparisonOperator, ast::ComparisonOperator::Greater);
    EXPECT_EQ(comparison(age >= 18).comparisonOperator, ast::ComparisonOperator::GreaterOrEqual);
    EXPECT_EQ(comparison(age < 18).comparisonOperator, ast::ComparisonOperator::Less);
    EXPECT_EQ(comparison(age <= 18).comparisonOperator, ast::ComparisonOperator::LessOrEqual);
    EXPECT_EQ(std::get<int>(comparison(age == short{18}).value.get()), 18);
    EXPECT_EQ(std::get<double>(comparison(col<&User::score>() == 0.5F).value.get()), 0.5);
    EXPECT_EQ(std::get<long long>(comparison(col<&User::wide>() == 18).value.get()), 18);
}

TEST(TypedQueryTest, arithmeticValuesNormalizeToSupportedFieldTypes)
{
    const auto narrowCharacter = std::numeric_limits<char16_t>::max();
    const auto integer = comparison(col<&User::age>() == narrowCharacter).value;
    EXPECT_EQ(integer.getLogicalType(), orm::model::ColumnType::Int);
    EXPECT_EQ(std::get<int>(integer.get()), static_cast<int>(narrowCharacter));

    const auto wideCharacter = std::numeric_limits<char32_t>::max();
    const auto floating = comparison(col<&User::score>() == wideCharacter).value;
    EXPECT_EQ(floating.getLogicalType(), orm::model::ColumnType::Double);
    EXPECT_DOUBLE_EQ(std::get<double>(floating.get()), static_cast<double>(wideCharacter));

    checkFloatingNormalization(1.25L);
}

TEST(TypedQueryTest, textValuesAndPatternsRemainParameters)
{
    const auto name = col<&User::name>();
    const std::string text{"Ada"};
    EXPECT_EQ(std::get<std::string>(comparison(name == text).value.get()), text);
    EXPECT_EQ(std::get<std::string>(comparison(name == std::string_view{text}).value.get()), text);
    EXPECT_EQ(std::get<std::string>(comparison(name == "Ada").value.get()), text);
    EXPECT_EQ(comparison(name.like("A%")).comparisonOperator, ast::ComparisonOperator::Like);
    EXPECT_EQ(comparison(name.notLike("D%")).comparisonOperator, ast::ComparisonOperator::NotLike);
    EXPECT_EQ(std::get<std::string>(comparison(name.like("A%")).value.get()), "A%");
}

TEST(TypedQueryTest, nullCStringValuesFailBeforeBinding)
{
    const char* absent = nullptr;
    char* mutableAbsent = nullptr;
    const auto name = col<&User::name>();
    EXPECT_THROW((void)(name == absent), std::invalid_argument);
    EXPECT_THROW((void)(name != mutableAbsent), std::invalid_argument);
    EXPECT_THROW((void)name.like(absent), std::invalid_argument);
    EXPECT_THROW((void)name.notLike(mutableAbsent), std::invalid_argument);
    EXPECT_THROW((void)name.in(std::vector<const char*>{"Ada", absent}), std::invalid_argument);
    EXPECT_THROW((void)name.notIn({mutableAbsent}), std::invalid_argument);
    EXPECT_THROW((void)name.between("Ada", absent), std::invalid_argument);
    EXPECT_THROW((void)name.notBetween(mutableAbsent, "Ada"), std::invalid_argument);
    EXPECT_THROW((void)(min(name) == absent), std::invalid_argument);

    orm::Update<User> update;
    EXPECT_THROW((void)update.set(col<&User::email>(), absent), std::invalid_argument);
    EXPECT_TRUE(orm::FakeDatabase::getUpdateSpec(update).assignments.empty());
}

TEST(TypedQueryTest, nullableFieldAndNullableRelationProduceNullExpressions)
{
    const auto email = col<&User::email>();
    const auto isNull = ast::erase(email.isNull());
    const auto isNotNull = ast::erase(email.isNotNull());
    EXPECT_EQ(std::get<ast::NullExpression>(isNull.getNode().expression).nullOperator, ast::NullOperator::IsNull);
    EXPECT_EQ(std::get<ast::NullExpression>(isNotNull.getNode().expression).nullOperator, ast::NullOperator::IsNotNull);
    EXPECT_TRUE(std::holds_alternative<ast::NullExpression>(ast::erase(email == nullptr).getNode().expression));
    EXPECT_TRUE(std::holds_alternative<ast::NullExpression>(ast::erase(email != nullptr).getNode().expression));
    EXPECT_TRUE(std::holds_alternative<ast::NullExpression>(ast::erase(email == std::nullopt).getNode().expression));
    EXPECT_TRUE(std::holds_alternative<ast::NullExpression>(ast::erase(email != std::nullopt).getNode().expression));
    const auto relatedNull = ast::erase(col<&User::profile, &Profile::id>().isNull());
    EXPECT_EQ(std::get<ast::NullExpression>(relatedNull.getNode().expression).column.getPath(), "profile.id");
}

TEST(TypedQueryTest, listsSupportInitializerListsAndVectors)
{
    const auto age = col<&User::age>();
    const auto included = ast::erase(age.in({18, 19}));
    const auto excluded = ast::erase(age.notIn({20, 21}));
    EXPECT_EQ(std::get<ast::ListExpression>(included.getNode().expression).listOperator, ast::ListOperator::In);
    EXPECT_EQ(std::get<ast::ListExpression>(excluded.getNode().expression).listOperator, ast::ListOperator::NotIn);
    const auto vectorIncluded = ast::erase(age.in(std::vector<short>{18, 19}));
    const auto vectorExcluded = ast::erase(age.notIn(std::vector<int>{20, 21}));
    EXPECT_EQ(std::get<ast::ListExpression>(vectorIncluded.getNode().expression).values.size(), 2);
    EXPECT_EQ(std::get<int>(std::get<ast::ListExpression>(vectorExcluded.getNode().expression).values.front().get()),
              20);
    EXPECT_THROW((void)age.in(std::vector<int>{}), std::invalid_argument);
    EXPECT_THROW((void)age.notIn(std::vector<int>{}), std::invalid_argument);
}

TEST(TypedQueryTest, rangesPreserveBoundOrder)
{
    const auto age = col<&User::age>();
    const auto inclusive = ast::erase(age.between(short{18}, 30));
    const auto exclusive = ast::erase(age.notBetween(18, short{30}));
    const auto& range = std::get<ast::BetweenExpression>(inclusive.getNode().expression);
    EXPECT_EQ(range.betweenOperator, ast::BetweenOperator::Between);
    EXPECT_EQ(std::get<int>(range.lowerValue.get()), 18);
    EXPECT_EQ(std::get<int>(range.upperValue.get()), 30);
    EXPECT_EQ(std::get<ast::BetweenExpression>(exclusive.getNode().expression).betweenOperator,
              ast::BetweenOperator::NotBetween);
}

TEST(TypedQueryTest, booleanVectorsBindValuesWithoutProxyReferences)
{
    const auto active = col<&User::active>();
    const auto included = ast::erase(active.in(std::vector<bool>{true, false}));
    const auto excluded = ast::erase(active.notIn(std::vector<bool>{false, true}));
    const auto& inValues = std::get<ast::ListExpression>(included.getNode().expression).values;
    const auto& notInValues = std::get<ast::ListExpression>(excluded.getNode().expression).values;
    ASSERT_EQ(inValues.size(), 2);
    ASSERT_EQ(notInValues.size(), 2);
    EXPECT_EQ(std::get<int>(inValues[0].get()), 1);
    EXPECT_EQ(std::get<int>(inValues[1].get()), 0);
    EXPECT_EQ(std::get<int>(notInValues[0].get()), 0);
    EXPECT_EQ(std::get<int>(notInValues[1].get()), 1);
    EXPECT_THROW((void)active.in(std::vector<bool>{}), std::invalid_argument);
    EXPECT_THROW((void)active.notIn(std::vector<bool>{}), std::invalid_argument);
}

TEST(TypedQueryTest, logicalCompositionRetainsNestedStructure)
{
    const auto left = col<&User::age>() >= 18;
    const auto right = col<&User::name>() == "Ada";
    const auto conjunction = ast::erase(left && right);
    EXPECT_EQ(std::get<ast::LogicalExpression>(conjunction.getNode().expression).logicalOperator,
              ast::LogicalOperator::And);
    const auto disjunction = ast::erase(left || right);
    EXPECT_EQ(std::get<ast::LogicalExpression>(disjunction.getNode().expression).logicalOperator,
              ast::LogicalOperator::Or);
    EXPECT_TRUE(std::holds_alternative<ast::NotExpression>(ast::erase(!left).getNode().expression));
}

TEST(TypedQueryTest, orderingAndProjectionsRetainSources)
{
    EXPECT_EQ(ast::erase(asc(col<&User::age>())).direction, ast::OrderDirection::Asc);
    EXPECT_EQ(ast::erase(desc(col<&User::age>())).direction, ast::OrderDirection::Desc);
    const auto projection = ast::erase(as("name", col<&User::name>()));
    EXPECT_EQ(projection.resultField, "name");
    EXPECT_EQ(std::get<ast::Column>(projection.source).getPath(), "name");
    const auto aggregateProjection = ast::erase(as("count", countAll<User>()));
    EXPECT_EQ(std::get<ast::AggregateExpression>(aggregateProjection.source).function,
              ast::AggregateFunction::CountAll);
}

TEST(TypedQueryTest, aggregatesPreserveFunctionAndColumn)
{
    EXPECT_EQ(ast::erase(count(col<&User::name>())).function, ast::AggregateFunction::Count);
    EXPECT_EQ(ast::erase(sum(col<&User::age>())).function, ast::AggregateFunction::Sum);
    EXPECT_EQ(ast::erase(avg(col<&User::age>())).function, ast::AggregateFunction::Avg);
    EXPECT_EQ(ast::erase(min(col<&User::age>())).function, ast::AggregateFunction::Min);
    EXPECT_EQ(ast::erase(max(col<&User::age>())).function, ast::AggregateFunction::Max);
    EXPECT_FALSE(ast::erase(countAll<User>()).column.has_value());
    EXPECT_EQ(ast::erase(sum(col<&User::age>())).column->getPath(), "age");
}

TEST(TypedQueryTest, aggregatePredicatesPreserveOperatorsAndLogicalComposition)
{
    const auto users = countAll<User>();
    const auto predicates = std::vector{ast::erase(users == 1), ast::erase(users != 1), ast::erase(users > 1),
                                        ast::erase(users >= 1), ast::erase(users < 1),  ast::erase(users <= 1)};
    const auto operators = std::vector{ast::ComparisonOperator::Equal,   ast::ComparisonOperator::NotEqual,
                                       ast::ComparisonOperator::Greater, ast::ComparisonOperator::GreaterOrEqual,
                                       ast::ComparisonOperator::Less,    ast::ComparisonOperator::LessOrEqual};
    for (std::size_t index = 0; index < predicates.size(); ++index)
    {
        EXPECT_EQ(
            std::get<ast::AggregateComparisonExpression>(predicates[index].getNode().expression).comparisonOperator,
            operators[index]);
    }
    EXPECT_TRUE(std::holds_alternative<ast::AggregateLogicalExpression>(
        ast::erase((users > 1) && (users < 10)).getNode().expression));
    EXPECT_TRUE(std::holds_alternative<ast::AggregateLogicalExpression>(
        ast::erase((users == 1) || (users == 2)).getNode().expression));
    EXPECT_TRUE(std::holds_alternative<ast::AggregateNotExpression>(ast::erase(!(users == 1)).getNode().expression));
}

TEST(TypedQueryTest, collectionPredicatesUseMemberNamesAndTargetPredicates)
{
    const auto matches = ast::erase(any<&User::roles>(col<&Role::name>() == "admin"));
    const auto missing = ast::erase(none<&User::roles>(col<&Role::name>() == "admin"));
    const auto existsOnly = ast::erase(exists<&User::roles>());
    const auto& collection = std::get<ast::CollectionExpression>(matches.getNode().expression);
    EXPECT_EQ(collection.relation, "roles");
    EXPECT_EQ(collection.collectionOperator, ast::CollectionOperator::Any);
    EXPECT_NE(collection.predicate, nullptr);
    EXPECT_EQ(std::get<ast::CollectionExpression>(missing.getNode().expression).collectionOperator,
              ast::CollectionOperator::None);
    EXPECT_EQ(std::get<ast::CollectionExpression>(existsOnly.getNode().expression).collectionOperator,
              ast::CollectionOperator::Exists);
    EXPECT_EQ(std::get<ast::CollectionExpression>(existsOnly.getNode().expression).predicate, nullptr);
}

TEST(TypedQueryTest, rawFragmentsRequireExplicitModelAndPreserveParameters)
{
    const auto predicate = ast::erase(raw<User>("name = :name", param("name", "Ada")));
    const auto& expression = std::get<ast::RawExpression>(predicate.getNode().expression);
    EXPECT_EQ(expression.sql, "name = :name");
    ASSERT_EQ(expression.parameters.size(), 1);
    EXPECT_EQ(expression.parameters.front().name, "name");
    EXPECT_EQ(std::get<std::string>(expression.parameters.front().value.get()), "Ada");
    const auto order = ast::erase(rawOrder<User>("LOWER(name) ASC"));
    EXPECT_TRUE(order.isRaw);
    EXPECT_EQ(order.rawSql, "LOWER(name) ASC");
}

TEST(TypedQueryTest, nullableAssignmentsStorePresentAndAbsentOptionals)
{
    orm::Update<User> update;
    update.set(col<&User::rank>(), std::optional<short>{2})
        .set(col<&User::email>(), std::optional<std::string>{})
        .set(col<&User::rank>(), std::nullopt)
        .set(col<&User::email>(), "Ada")
        .where(col<&User::id>() == 1);
    const auto& data = orm::FakeDatabase::getUpdateSpec(update);
    ASSERT_EQ(data.assignments.size(), 4);
    ASSERT_TRUE(data.assignments[0].value.value.has_value());
    EXPECT_EQ(std::get<int>(data.assignments[0].value.value->get()), 2);
    EXPECT_FALSE(data.assignments[1].value.value.has_value());
    EXPECT_FALSE(data.assignments[2].value.value.has_value());
    EXPECT_EQ(std::get<std::string>(data.assignments[3].value.value->get()), "Ada");
}

TEST(TypedQueryTest, updateCompositionAcceptsInitialAndExistingPredicates)
{
    orm::Update<User> conjunction;
    conjunction.andWhere(col<&User::id>() == 1);
    ASSERT_TRUE(orm::FakeDatabase::getUpdateSpec(conjunction).predicate.has_value());
    EXPECT_TRUE(std::holds_alternative<ast::ComparisonExpression>(
        orm::FakeDatabase::getUpdateSpec(conjunction).predicate->getNode().expression));
    conjunction.andWhere(col<&User::age>() >= 18);
    EXPECT_EQ(
        std::get<ast::LogicalExpression>(orm::FakeDatabase::getUpdateSpec(conjunction).predicate->getNode().expression)
            .logicalOperator,
        ast::LogicalOperator::And);

    orm::Update<User> disjunction;
    disjunction.orWhere(col<&User::id>() == 1);
    ASSERT_TRUE(orm::FakeDatabase::getUpdateSpec(disjunction).predicate.has_value());
    EXPECT_TRUE(std::holds_alternative<ast::ComparisonExpression>(
        orm::FakeDatabase::getUpdateSpec(disjunction).predicate->getNode().expression));
    disjunction.orWhere(col<&User::id>() == 2);
    EXPECT_EQ(
        std::get<ast::LogicalExpression>(orm::FakeDatabase::getUpdateSpec(disjunction).predicate->getNode().expression)
            .logicalOperator,
        ast::LogicalOperator::Or);

    disjunction.where(col<&User::id>() == 3);
    EXPECT_EQ(std::get<int>(std::get<ast::ComparisonExpression>(
                                orm::FakeDatabase::getUpdateSpec(disjunction).predicate->getNode().expression)
                                .value.get()),
              3);
}
