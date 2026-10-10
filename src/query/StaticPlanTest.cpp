module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

using namespace orm::query;
using namespace static_plan_models;
namespace ast = orm::query::detail;

namespace
{
inline constexpr auto staticPlanBase = select<User>();
inline constexpr auto adults = staticPlanBase.where(col<&User::age>() >= param<int, 0>())
                                   .orderBy(asc(col<&User::id>()))
                                   .limit(param<std::size_t, 1>())
                                   .offset(param<std::size_t, 2>());
inline constexpr auto renameUser =
    update<User>().set(col<&User::name>(), param<std::string, 0>()).where(col<&User::id>() == param<int, 1>());
inline constexpr auto eraseUser = remove<User>().where(col<&User::id>() == param<int, 0>());

static_assert(not decltype(staticPlanBase)::hasPredicate);
static_assert(decltype(adults)::hasPredicate);
static_assert(decltype(adults)::hasLimit && decltype(adults)::hasOffset);
static_assert(std::same_as<typename decltype(adults)::Model, User>);
static_assert(std::same_as<typename decltype(adults)::Result, User>);
static_assert(not std::is_default_constructible_v<std::remove_cvref_t<decltype(adults)>>);
static_assert(not std::same_as<decltype(staticPlanBase), decltype(adults)>);
static_assert(std::tuple_size_v<typename decltype(renameUser)::Assignments> == 1);

auto comparison(auto& query) -> const ast::ComparisonExpression&
{
    const auto& data = orm::detail::QueryTestAccess::getSelectSpec(query);
    return std::get<ast::ComparisonExpression>(data.predicate->getNode().expression);
}
} // namespace

TEST(StaticPlanTest, clausesPreserveTheOriginalPlanAndOwnValues)
{
    const auto filtered = staticPlanBase.where(col<&User::name>() == std::string{"Ada"});
    auto original = staticPlanBase.toDynamic();
    auto query = filtered.toDynamic();
    EXPECT_FALSE(orm::detail::QueryTestAccess::getSelectSpec(original).predicate.has_value());
    EXPECT_EQ(std::get<std::string>(comparison(query).value.get()), "Ada");
}

TEST(StaticPlanTest, executionArgumentsBindWithoutChangingTheReusablePlan)
{
    auto first = adults.toDynamic(short{18}, std::size_t{10}, std::size_t{0});
    auto second = adults.toDynamic(21, std::size_t{20}, std::size_t{5});
    EXPECT_EQ(std::get<int>(comparison(first).value.get()), 18);
    EXPECT_EQ(std::get<int>(comparison(second).value.get()), 21);
    const auto& firstData = orm::detail::QueryTestAccess::getSelectSpec(first);
    const auto& secondData = orm::detail::QueryTestAccess::getSelectSpec(second);
    ASSERT_EQ(firstData.orderBy.size(), 1);
    EXPECT_EQ(firstData.orderBy.front().column.getPath(), "id");
    EXPECT_EQ(firstData.limit, 10);
    EXPECT_EQ(firstData.offset, 0);
    EXPECT_EQ(secondData.limit, 20);
    EXPECT_EQ(secondData.offset, 5);
}

TEST(StaticPlanTest, repeatedSlotsReuseOneArgumentAndPreserveEachOccurrence)
{
    constexpr auto plan =
        select<User>().where((col<&User::age>() >= param<int, 0>()) && (col<&User::id>() != param<int, 0>()));
    auto query = plan.toDynamic(18);
    const auto& data = orm::detail::QueryTestAccess::getSelectSpec(query);
    const auto& logical = std::get<ast::LogicalExpression>(data.predicate->getNode().expression);
    EXPECT_EQ(std::get<int>(std::get<ast::ComparisonExpression>(logical.left->expression).value.get()), 18);
    EXPECT_EQ(std::get<int>(std::get<ast::ComparisonExpression>(logical.right->expression).value.get()), 18);
}

TEST(StaticPlanTest, logicalClauseMethodsHandleAnEmptyAndAnExistingClause)
{
    auto conjunction = select<User>().andWhere(col<&User::age>() > 18).andWhere(col<&User::id>() != 1).toDynamic();
    auto disjunction = select<User>().orWhere(col<&User::age>() > 18).orWhere(col<&User::id>() == 1).toDynamic();
    const auto& andData = orm::detail::QueryTestAccess::getSelectSpec(conjunction);
    const auto& orData = orm::detail::QueryTestAccess::getSelectSpec(disjunction);
    EXPECT_EQ(std::get<ast::LogicalExpression>(andData.predicate->getNode().expression).logicalOperator,
              ast::LogicalOperator::And);
    EXPECT_EQ(std::get<ast::LogicalExpression>(orData.predicate->getNode().expression).logicalOperator,
              ast::LogicalOperator::Or);
}

TEST(StaticPlanTest, projectionsRetainLiteralAliasesAndReplaceTheProjectionClause)
{
    constexpr auto plan = selectAs<User, UserName>(as<"old">(col<&User::name>()))
                              .project(as<"name">(col<&User::name>()))
                              .where(col<&User::age>() >= param<int, 0>())
                              .distinct()
                              .disableJoining();
    auto query = plan.toDynamic(18);
    const auto& data = orm::detail::QueryTestAccess::getSelectSpec(query);
    ASSERT_EQ(data.projections.size(), 1);
    EXPECT_EQ(data.projections.front().resultField, "name");
    EXPECT_TRUE(data.isDistinct);
    EXPECT_FALSE(data.shouldJoin);
}

TEST(StaticPlanTest, groupingAndHavingHandleInitialAndComposedExpressions)
{
    constexpr auto plan = selectAs<User, UserName>(as<"name">(col<&User::name>()))
                              .groupBy(col<&User::name>())
                              .andHaving(countAll<User>() >= param<long long, 0>())
                              .andHaving(sum(col<&User::age>()) > 0)
                              .orHaving(max(col<&User::age>()) < 100);
    auto query = plan.toDynamic(1);
    const auto& data = orm::detail::QueryTestAccess::getSelectSpec(query);
    ASSERT_EQ(data.groupBy.size(), 1);
    EXPECT_EQ(data.groupBy.front().getPath(), "name");
    ASSERT_TRUE(data.having.has_value());
    EXPECT_EQ(std::get<ast::AggregateLogicalExpression>(data.having->getNode().expression).logicalOperator,
              ast::LogicalOperator::Or);

    auto initialOr = select<User>().orHaving(countAll<User>() > 0).toDynamic();
    EXPECT_TRUE(orm::detail::QueryTestAccess::getSelectSpec(initialOr).having.has_value());
}

TEST(StaticPlanTest, collectionIncludesAreRetainedOnlyOnce)
{
    constexpr auto included = select<User>().include<&User::roles>().include<&User::roles>();
    static_assert(std::tuple_size_v<typename decltype(included)::Includes> == 1);
    auto query = included.toDynamic();
    const auto& data = orm::detail::QueryTestAccess::getSelectSpec(query);
    ASSERT_EQ(data.includes.size(), 1);
    EXPECT_EQ(data.includes.front(), "roles");
}

TEST(StaticPlanTest, mutationArgumentsAndOptionalValuesBecomeConcreteAssignments)
{
    auto rename = renameUser.toDynamic(std::string{"Grace"}, 1);
    const auto& renameData = orm::detail::QueryTestAccess::getUpdateSpec(rename);
    ASSERT_EQ(renameData.assignments.size(), 1);
    EXPECT_EQ(std::get<std::string>(renameData.assignments.front().value.value->get()), "Grace");

    constexpr auto nullable = update<User>()
                                  .set(col<&User::email>(), param<std::optional<std::string>, 0>())
                                  .where(col<&User::id>() == param<int, 1>());
    auto empty = nullable.toDynamic(std::optional<std::string>{}, 1);
    auto present = nullable.toDynamic(std::optional<std::string>{"ada@example.test"}, 1);
    EXPECT_FALSE(orm::detail::QueryTestAccess::getUpdateSpec(empty).assignments.front().value.value.has_value());
    EXPECT_EQ(std::get<std::string>(
                  orm::detail::QueryTestAccess::getUpdateSpec(present).assignments.front().value.value->get()),
              "ada@example.test");

    auto nullPointer = update<User>().set(col<&User::email>(), nullptr).where(col<&User::id>() == 1).toDynamic();
    auto nullOptional = update<User>().set(col<&User::email>(), std::nullopt).where(col<&User::id>() == 1).toDynamic();
    EXPECT_FALSE(orm::detail::QueryTestAccess::getUpdateSpec(nullPointer).assignments.front().value.value.has_value());
    EXPECT_FALSE(orm::detail::QueryTestAccess::getUpdateSpec(nullOptional).assignments.front().value.value.has_value());
}

TEST(StaticPlanTest, removeConversionReturnsTheBoundTypedPredicate)
{
    const auto predicate = ast::erase(eraseUser.toDynamic(2));
    EXPECT_EQ(std::get<int>(std::get<ast::ComparisonExpression>(predicate.getNode().expression).value.get()), 2);
}

TEST(StaticPlanTest, fixedListsAndRuntimeListsBindTheirActualElements)
{
    constexpr int ids[]{1, 2};
    auto fixed = select<User>().where(col<&User::id>().in(std::array{1, 2})).toDynamic();
    auto cArray = select<User>().where(col<&User::id>().in(ids)).toDynamic();
    auto variadic = select<User>().where(col<&User::id>().in(1, short{2})).toDynamic();
    auto runtime = select<User>().where(col<&User::id>().in(std::vector{1, 2})).toDynamic();
    auto initializer = select<User>().where(col<&User::id>().notIn({1, 2})).toDynamic();
    auto parameter =
        select<User>().where(col<&User::id>().in(param<std::array<int, 2>, 0>())).toDynamic(std::array{1, 2});
    auto vectorParameter =
        select<User>().where(col<&User::id>().notIn(param<std::vector<int>, 0>())).toDynamic(std::vector{1, 2});
    for (auto* query : {&fixed, &cArray, &variadic, &runtime, &initializer, &parameter, &vectorParameter})
    {
        const auto& data = orm::detail::QueryTestAccess::getSelectSpec(*query);
        const auto& list = std::get<ast::ListExpression>(data.predicate->getNode().expression);
        ASSERT_EQ(list.values.size(), 2);
        EXPECT_EQ(std::get<int>(list.values[0].get()), 1);
        EXPECT_EQ(std::get<int>(list.values[1].get()), 2);
    }
}

TEST(StaticPlanTest, rawShapesRemainConvertibleToMutableQueries)
{
    auto query =
        select<User>().where(raw<User>("age >= :age", param("age", 18))).orderBy(rawOrder<User>("id DESC")).toDynamic();
    const auto& data = orm::detail::QueryTestAccess::getSelectSpec(query);
    EXPECT_TRUE(std::holds_alternative<ast::RawExpression>(data.predicate->getNode().expression));
    ASSERT_EQ(data.orderBy.size(), 1);
    EXPECT_TRUE(data.orderBy.front().isRaw);
}

TEST(StaticPlanTest, paginationRejectsValuesAboveSigned64BitRange)
{
    if constexpr (std::numeric_limits<std::size_t>::max() >
                  static_cast<std::size_t>(std::numeric_limits<long long>::max()))
    {
        const auto excessive = std::numeric_limits<std::size_t>::max();
        EXPECT_THROW((void)select<User>().limit(excessive).toDynamic(), std::out_of_range);
        EXPECT_THROW((void)select<User>().offset(excessive).toDynamic(), std::out_of_range);
        EXPECT_THROW((void)adults.toDynamic(18, excessive, std::size_t{0}), std::out_of_range);
    }
}

namespace
{
auto expectParametersEqual(const std::vector<orm::db::StatementParameter>& left,
                           const std::vector<orm::db::StatementParameter>& right) -> void
{
    ASSERT_EQ(left.size(), right.size());
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        EXPECT_EQ(left[i].name, right[i].name);
        EXPECT_EQ(left[i].getLogicalType(), right[i].getLogicalType());
        EXPECT_EQ(left[i].getBoundValue().value, right[i].getBoundValue().value);
    }
}
} // namespace

TEST(StaticPlanTest, constexprCacheMatchesRuntimeSqlForRelationsCollectionsAndHaving)
{
    constexpr auto plan =
        selectAs<User, UserStats>(as<"city">(col<&User::profile, &Profile::city>()), as<"users">(countAll<User>()),
                                  as<"totalAge">(sum(col<&User::age>())), as<"averageAge">(avg(col<&User::age>())),
                                  as<"minimumAge">(min(col<&User::age>())), as<"maximumAge">(max(col<&User::age>())))
            .where((col<&User::age>() >= param<int, 0>()) &&
                   any<&User::roles>(col<&Role::name>() == param<std::string, 1>()))
            .groupBy(col<&User::profile, &Profile::city>())
            .having(countAll<User>() >= param<long long, 2>())
            .orderBy(asc(col<&User::profile, &Profile::city>()));
    using Plan = std::remove_cvref_t<decltype(plan)>;
    constexpr auto& sqlite = ast::compiledStatement<Schema, Plan, orm::db::CompiledSqlFlavor::SQLite>;
    constexpr auto& postgres = ast::compiledStatement<Schema, Plan, orm::db::CompiledSqlFlavor::PostgreSQL>;
    static_assert(sqlite.valid && postgres.valid);
    static_assert(sqlite.view().find("EXISTS") != std::string_view::npos);
    static_assert(sqlite.view().find("GROUP BY") != std::string_view::npos);
    static_assert(postgres.view().find("HAVING") != std::string_view::npos);
    static_assert(sqlite.program.bindings.size() == 3);
    auto dynamic = plan.toDynamic(18, std::string{"reader"}, 2);
    const orm::db::sqlite::SqliteBackend backend;
    const auto rendered = backend.commandGenerator().select(orm::modelView<Schema, User>(),
                                                            orm::detail::QueryTestAccess::getSelectSpec(dynamic));
    EXPECT_EQ(sqlite.view(), rendered.sql);
    expectParametersEqual(ast::collectParameters(plan, std::tuple{18, std::string{"reader"}, 2}), rendered.parameters);
    EXPECT_EQ(sqlite.view().data(),
              (ast::compiledStatement<Schema, Plan, orm::db::CompiledSqlFlavor::SQLite>.view().data()));
}

TEST(StaticPlanTest, cacheBindingsPreserveRepeatedSlotsAndNullableAssignments)
{
    constexpr auto patch = update<User>()
                               .set(col<&User::age>(), param<int, 0>())
                               .set(col<&User::email>(), param<std::optional<std::string>, 1>())
                               .where(col<&User::id>() == param<int, 0>());
    using Plan = std::remove_cvref_t<decltype(patch)>;
    constexpr auto& compiled = ast::compiledStatement<Schema, Plan, orm::db::CompiledSqlFlavor::SQLite>;
    static_assert(compiled.valid);
    static_assert(compiled.program.bindings.size() == 3);
    auto dynamic = patch.toDynamic(1, std::optional<std::string>{});
    const orm::db::sqlite::SqliteBackend backend;
    const auto rendered = backend.commandGenerator().update(orm::modelView<Schema, User>(),
                                                            orm::detail::QueryTestAccess::getUpdateSpec(dynamic));
    EXPECT_EQ(compiled.view(), rendered.sql);
    const auto parameters = ast::collectParameters(patch, std::tuple{1, std::optional<std::string>{}});
    expectParametersEqual(parameters, rendered.parameters);
    ASSERT_EQ(parameters.size(), 3);
    EXPECT_TRUE(parameters[1].getBoundValue().isNull());
    EXPECT_EQ(parameters[1].getLogicalType(), orm::model::ColumnType::String);
    EXPECT_EQ(parameters[0].getBoundValue().value, parameters[2].getBoundValue().value);
}

TEST(StaticPlanTest, runtimeShapesAndInvalidJoinShapesDoNotUseTheCache)
{
    const auto runtime = select<User>().where(col<&User::id>().in(std::vector{1}));
    const auto rawShape = select<User>().where(raw<User>("id = :id", param("id", 1)));
    const auto rawOrderShape = select<User>().orderBy(rawOrder<User>("id"));
    const auto alias = selectAs<User, UserName>(as("name", col<&User::name>()));
    static_assert(not ast::compiledSqlEligible<std::remove_cvref_t<decltype(runtime)>>);
    static_assert(not ast::compiledSqlEligible<std::remove_cvref_t<decltype(rawShape)>>);
    static_assert(not ast::compiledSqlEligible<std::remove_cvref_t<decltype(rawOrderShape)>>);
    static_assert(not ast::compiledSqlEligible<std::remove_cvref_t<decltype(alias)>>);
    constexpr auto invalidJoin =
        select<User>().where(col<&User::profile, &Profile::city>() == "Paris").disableJoining();
    using InvalidPlan = std::remove_cvref_t<decltype(invalidJoin)>;
    static_assert(not ast::compiledStatement<Schema, InvalidPlan, orm::db::CompiledSqlFlavor::SQLite>.valid);
}
