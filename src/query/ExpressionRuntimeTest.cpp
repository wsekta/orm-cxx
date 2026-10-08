#include <array>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include "orm-cxx/query/OrderBy.hpp"
#include "orm-cxx/query/Projection.hpp"

namespace expression_runtime_models
{
struct Role
{
    int id;
    std::string name;
};
struct User
{
    int id;
    int age;
    double score;
    bool active;
    std::string name;
    std::optional<std::string> email;
    orm::ManyToMany<Role> roles;
};
} // namespace expression_runtime_models

using namespace orm::query;
using namespace expression_runtime_models;
namespace expression_detail = orm::query::detail;

namespace
{
inline constexpr auto namedExpression = col<&User::name>() == "Alice";
inline constexpr auto slottedExpression =
    (col<&User::age>() >= param<int, 0>()) && (col<&User::score>() <= param<int, 0>());
static_assert(decltype(namedExpression)::staticSqlEligible);
static_assert(std::tuple_size_v<expression_detail::ParameterTypes<decltype(slottedExpression)>> == 2);
static_assert(!std::is_default_constructible_v<std::remove_cvref_t<decltype(namedExpression)>>);
static_assert(!expression_detail::ORM_QUERY_BOUND<decltype(slottedExpression)>);
static_assert(expression_detail::parameterCompatible<std::array<int, 2>, std::array<short, 2>>());
static_assert(!expression_detail::parameterCompatible<std::vector<int>, std::array<int, 2>>());
static_assert(!expression_detail::parameterCompatible<std::vector<int>, std::initializer_list<int>>());
static_assert(expression_detail::parameterCompatible<std::optional<std::string>, std::optional<std::string_view>>());
static_assert(expression_detail::parameterCompatible<std::optional<int>, short>());

auto valuesOf(const auto& expression, const auto& args) -> std::vector<QueryValue>
{
    std::vector<QueryValue> result;
    expression_detail::visitValues(expression, args, [&](QueryValue value) { result.push_back(std::move(value)); });
    return result;
}
} // namespace

TEST(ExpressionRuntimeTest, literalAndRuntimeTextHaveIndependentStorage)
{
    auto literal = namedExpression.dynamic();
    const auto runtime = expression_detail::erase(literal);
    EXPECT_EQ(std::get<std::string>(
                  std::get<expression_detail::ComparisonExpression>(runtime.getNode().expression).value.get()),
              "Alice");

    std::string backing{"before"};
    auto expression = col<&User::name>() == std::string_view{backing};
    backing = "after";
    const auto values = valuesOf(expression, std::tuple<>{});
    ASSERT_EQ(values.size(), 1U);
    EXPECT_EQ(std::get<std::string>(values.front().get()), "before");

    char buffer[]{'a', 'b', 'c', '\0'};
    auto captured = col<&User::name>() == buffer;
    buffer[0] = 'z';
    EXPECT_EQ(std::get<std::string>(valuesOf(captured, std::tuple<>{}).front().get()), "abc");
}

TEST(ExpressionRuntimeTest, optionalTextCaptureOwnsViewsAndPointers)
{
    std::string backing{"before"};
    const auto view = expression_detail::captureValue(std::optional<std::string_view>{backing});
    const auto pointer = expression_detail::captureValue(std::optional<const char*>{backing.c_str()});
    static_assert(std::same_as<std::remove_cvref_t<decltype(view)>, std::optional<std::string>>);
    static_assert(std::same_as<std::remove_cvref_t<decltype(pointer)>, std::optional<std::string>>);
    backing = "after";
    EXPECT_EQ(view.value(), "before");
    EXPECT_EQ(pointer.value(), "before");
    EXPECT_FALSE(expression_detail::captureValue(std::optional<std::string_view>{}).has_value());
    EXPECT_FALSE(expression_detail::captureValue(std::optional<const char*>{}).has_value());
    EXPECT_THROW((void)expression_detail::captureValue(std::optional<const char*>{nullptr}), std::invalid_argument);
}

TEST(ExpressionRuntimeTest, stringPointersAreCopiedAndNullPointersAreRejected)
{
    char backing[]{'b', 'e', 'f', 'o', 'r', 'e', '\0'};
    const char* pointer = backing;
    const auto captured = col<&User::name>() == pointer;
    backing[0] = 'z';
    EXPECT_EQ(std::get<std::string>(valuesOf(captured, std::tuple<>{}).front().get()), "before");

    const char* nullPointer = nullptr;
    EXPECT_THROW((void)(col<&User::name>() == nullPointer), std::invalid_argument);
    const auto slotted = col<&User::name>() == param<std::string, 0>();
    expression_detail::validateParameters<decltype(slotted), const char*>();
    EXPECT_THROW((void)valuesOf(slotted, std::tuple{nullPointer}), std::invalid_argument);
    EXPECT_THROW((void)expression_detail::bindExpression(slotted, std::tuple{nullPointer}), std::invalid_argument);
}

TEST(ExpressionRuntimeTest, fixedAndVariableListsOwnTextElements)
{
    std::string backing{"before"};
    const auto arrays = col<&User::name>().in(std::array<std::string_view, 1>{backing});
    const char* pointers[]{backing.c_str()};
    const auto cArrays = col<&User::name>().in(pointers);
    const auto vectors = col<&User::name>().notIn(std::vector<std::string_view>{backing});
    backing = "after";
    EXPECT_EQ(std::get<std::string>(valuesOf(arrays, std::tuple<>{}).front().get()), "before");
    EXPECT_EQ(std::get<std::string>(valuesOf(cArrays, std::tuple<>{}).front().get()), "before");
    EXPECT_EQ(std::get<std::string>(valuesOf(vectors, std::tuple<>{}).front().get()), "before");
    EXPECT_TRUE(decltype(arrays)::staticSqlEligible);
    EXPECT_TRUE(decltype(cArrays)::staticSqlEligible);
    EXPECT_FALSE(decltype(vectors)::staticSqlEligible);
}

TEST(ExpressionRuntimeTest, repeatedSlotsNormalizeAtEveryField)
{
    expression_detail::validateParameters<decltype(slottedExpression), short>();
    const auto values = valuesOf(slottedExpression, std::tuple{short{21}});
    ASSERT_EQ(values.size(), 2U);
    EXPECT_EQ(values[0].getLogicalType(), orm::model::ColumnType::Int);
    EXPECT_EQ(std::get<int>(values[0].get()), 21);
    EXPECT_EQ(values[1].getLogicalType(), orm::model::ColumnType::Double);
    EXPECT_EQ(std::get<double>(values[1].get()), 21.0);
    EXPECT_TRUE(std::holds_alternative<expression_detail::LogicalExpression>(
        expression_detail::erase(expression_detail::bindExpression(slottedExpression, std::tuple{short{21}}))
            .getNode()
            .expression));
}

TEST(ExpressionRuntimeTest, parameterContainersBindTheirElementsAndRejectEmptyLists)
{
    const auto fixed = col<&User::age>().in(param<std::array<int, 2>, 0>());
    expression_detail::validateParameters<decltype(fixed), std::array<short, 2>>();
    const auto fixedValues = valuesOf(fixed, std::tuple{std::array<short, 2>{1, 2}});
    ASSERT_EQ(fixedValues.size(), 2U);
    EXPECT_EQ(std::get<int>(fixedValues[0].get()), 1);
    EXPECT_EQ(std::get<int>(fixedValues[1].get()), 2);

    const auto variable = col<&User::active>().notIn(param<std::vector<bool>, 0>());
    expression_detail::validateParameters<decltype(variable), std::vector<bool>>();
    const auto variableValues = valuesOf(variable, std::tuple{std::vector<bool>{true, false}});
    ASSERT_EQ(variableValues.size(), 2U);
    EXPECT_EQ(std::get<int>(variableValues[0].get()), 1);
    EXPECT_EQ(std::get<int>(variableValues[1].get()), 0);
    EXPECT_THROW((void)valuesOf(variable, std::tuple{std::vector<bool>{}}), std::invalid_argument);
    EXPECT_FALSE(decltype(variable)::staticSqlEligible);
}

TEST(ExpressionRuntimeTest, traversalFollowsNestedLogicalRangeAndCollectionOrder)
{
    const auto expression = !(col<&User::age>().between(param<int, 0>(), param<int, 1>()) ||
                              any<&User::roles>(col<&Role::id>() == param<int, 2>()));
    expression_detail::validateParameters<decltype(expression), int, short, int>();
    const auto values = valuesOf(expression, std::tuple{1, short{9}, 3});
    ASSERT_EQ(values.size(), 3U);
    EXPECT_EQ(std::get<int>(values[0].get()), 1);
    EXPECT_EQ(std::get<int>(values[1].get()), 9);
    EXPECT_EQ(std::get<int>(values[2].get()), 3);
    EXPECT_TRUE(std::holds_alternative<expression_detail::NotExpression>(
        expression_detail::erase(expression_detail::bindExpression(expression, std::tuple{1, short{9}, 3}))
            .getNode()
            .expression));
    EXPECT_TRUE(valuesOf(exists<&User::roles>(), std::tuple<>{}).empty());
    EXPECT_TRUE(valuesOf(col<&User::email>().isNull(), std::tuple<>{}).empty());
}

TEST(ExpressionRuntimeTest, namedFacadesSupportConstructionAssignmentAndDynamicComposition)
{
    TypedPredicate<User> predicate = col<&User::id>() == 1;
    predicate = col<&User::id>() == 2;
    auto logical = predicate && (col<&User::age>() > 18);
    EXPECT_FALSE(decltype(logical)::staticSqlEligible);
    EXPECT_TRUE(std::holds_alternative<expression_detail::LogicalExpression>(
        expression_detail::erase(logical.dynamic()).getNode().expression));

    TypedAggregate<User, long long, false> aggregate = countAll<User>();
    aggregate = count(col<&User::id>());
    TypedAggregatePredicate<User> having = aggregate > 1;
    having = countAll<User>() <= 2;
    auto combined = having || (min(col<&User::name>()) == "Alice");
    EXPECT_FALSE(decltype(combined)::staticSqlEligible);
    EXPECT_TRUE(std::holds_alternative<expression_detail::AggregateLogicalExpression>(
        expression_detail::erase(combined.dynamic()).getNode().expression));

    TypedOrderBy<User> order = asc(col<&User::age>());
    order = desc(col<&User::age>());
    EXPECT_EQ(expression_detail::erase(order.dynamic()).direction, expression_detail::OrderDirection::Desc);
    TypedProjection<User> projection = as<"age">(col<&User::age>());
    projection = as("name", col<&User::name>());
    EXPECT_EQ(expression_detail::erase(projection.dynamic()).resultField, "name");
}

TEST(ExpressionRuntimeTest, aggregateTraversalAndProjectionAliasesRetainTheirContracts)
{
    const auto predicate =
        !(sum(col<&User::age>()) >= param<long long, 0>()) && (max(col<&User::name>()) != param<std::string, 1>());
    expression_detail::validateParameters<decltype(predicate), short, const char*>();
    const auto values = valuesOf(predicate, std::tuple{short{1}, "Alice"});
    ASSERT_EQ(values.size(), 2U);
    EXPECT_EQ(std::get<long long>(values[0].get()), 1);
    EXPECT_EQ(std::get<std::string>(values[1].get()), "Alice");
    const auto named = as<"total">(sum(col<&User::age>()));
    const auto runtime = as("total", sum(col<&User::age>()));
    EXPECT_TRUE(decltype(named)::staticSqlEligible);
    EXPECT_FALSE(decltype(runtime)::staticSqlEligible);
    EXPECT_EQ(expression_detail::erase(named.dynamic()).resultField, "total");
    EXPECT_EQ(expression_detail::erase(runtime.dynamic()).resultField, "total");
    EXPECT_TRUE(valuesOf(named, std::tuple<>{}).empty());
    EXPECT_TRUE(valuesOf(runtime, std::tuple<>{}).empty());
    EXPECT_TRUE(valuesOf(asc(col<&User::age>()), std::tuple<>{}).empty());
}
