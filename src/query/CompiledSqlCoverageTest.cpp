#include <array>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "../database/defaults/SqlRenderer.hpp"
#include "orm-cxx/database/sqlite/SqliteBackend.hpp"
#include "orm-cxx/database/sqlite/SqliteDialect.hpp"
#include "orm-cxx/query/CompiledSql.hpp"
#include "tests/CollectionModelsDefinitions.hpp"
#include "tests/compile_fail/StaticPlanModels.hpp"
#include "tests/utils/FakeDatabase.hpp"

namespace
{
using namespace orm::query;
using namespace static_plan_models;
namespace sql = orm::db::detail;
namespace ast = orm::query::detail;
using Flavor = orm::db::CompiledSqlFlavor;

template <typename SchemaType = Schema, typename Plan>
auto expectRuntimeProgramMatchesCache(const Plan&) -> void
{
    const auto data = ast::compiled::program<Plan>();
    constexpr auto& sqlite = ast::compiledStatement<SchemaType, Plan, Flavor::SQLite>;
    constexpr auto& postgres = ast::compiledStatement<SchemaType, Plan, Flavor::PostgreSQL>;
    static_assert(sqlite.valid && postgres.valid);
    const auto model = orm::modelView<SchemaType, typename Plan::Model>();
    EXPECT_EQ(sql::emitSql(data.view(), model, sql::StaticSqlPolicy<Flavor::SQLite>{}), sqlite.view());
    EXPECT_EQ(sql::emitSql(data.view(), model, sql::StaticSqlPolicy<Flavor::PostgreSQL>{}), postgres.view());
    ASSERT_EQ(data.bindings.size(), sqlite.program.bindings.size());
    for (std::size_t i = 0; i < data.bindings.size(); ++i)
    {
        EXPECT_EQ(data.bindings[i].logicalType, sqlite.program.bindings[i].logicalType);
        EXPECT_EQ(data.bindings[i].index, sqlite.program.bindings[i].index);
    }
    EXPECT_EQ(data.includes.size(), sqlite.program.includes.size());
    EXPECT_EQ(data.nodes.size(), sqlite.program.nodes.size());
}

// Defensive renderer tests can mutate metadata independently of the public,
// statically validated Schema contract.
struct MutableSchema
{
    std::vector<std::vector<orm::model::ColumnView>> columns;
    std::vector<std::vector<orm::model::RelationView>> relations;
    std::vector<orm::model::ModelDataView> models;
    std::vector<const orm::model::ModelDataView*> pointers;
    orm::model::SchemaView schema;

    explicit MutableSchema(const orm::model::SchemaView& original)
    {
        for (const auto* model : original.models)
        {
            columns.emplace_back(model->columns.begin(), model->columns.end());
            relations.emplace_back(model->relations.begin(), model->relations.end());
            models.push_back(*model);
        }
        for (std::size_t i = 0; i < models.size(); ++i)
        {
            models[i].columns = columns[i];
            models[i].relations = relations[i];
            pointers.push_back(&models[i]);
        }
        schema.models = pointers;
    }
};

TEST(CompiledSqlCoverageTest, runtimeProgramAndFrozenCacheShareAllPredicateShapes)
{
    constexpr auto plan =
        select<User>()
            .where(((col<&User::age>().between(18, 80) && col<&User::id>().in(std::array{1, 2})) ||
                    !col<&User::email>().isNull()) &&
                   (any<&User::roles>(col<&Role::name>() == param<std::string, 0>()) || exists<&User::roles>()))
            .include<&User::roles>()
            .orderBy(asc(col<&User::name>()), desc(col<&User::id>()))
            .limit(param<std::size_t, 1>())
            .offset(param<std::size_t, 2>());
    expectRuntimeProgramMatchesCache(plan);
    constexpr auto oneToMany = select<collection_models::Author>().where(none<&collection_models::Author::books>(
        col<&collection_models::Book::author, &collection_models::Author::name>() == param<std::string, 0>()));
    expectRuntimeProgramMatchesCache<collection_models::Schema>(oneToMany);
}

TEST(CompiledSqlCoverageTest, runtimeProgramAndCacheShareProjectionGroupingAndMutationOrdering)
{
    constexpr auto grouped =
        selectAs<User, UserStats>(as<"city">(col<&User::profile, &Profile::city>()), as<"users">(countAll<User>()),
                                  as<"totalAge">(sum(col<&User::age>())), as<"averageAge">(avg(col<&User::age>())),
                                  as<"minimumAge">(min(col<&User::age>())), as<"maximumAge">(max(col<&User::age>())))
            .groupBy(col<&User::profile, &Profile::city>(), col<&User::id>())
            .having((countAll<User>() > param<long long, 0>()) || !(sum(col<&User::age>()) < 100));
    expectRuntimeProgramMatchesCache(grouped);
    constexpr auto patch = update<User>()
                               .set(col<&User::age>(), param<int, 0>())
                               .set(col<&User::email>(), param<std::optional<std::string>, 1>())
                               .where(col<&User::id>() == param<int, 0>());
    expectRuntimeProgramMatchesCache(patch);
    constexpr auto erase = remove<User>().where(none<&User::roles>(col<&Role::id>() == param<int, 0>()));
    expectRuntimeProgramMatchesCache(erase);
}

TEST(CompiledSqlCoverageTest, collectorOwnsPresentNullableValuesAndPreservesTypedNulls)
{
    constexpr auto patch = update<User>()
                               .set(col<&User::email>(), param<std::optional<std::string>, 0>())
                               .where(col<&User::id>() == param<int, 1>());
    const auto present = ast::collectParameters(patch, std::tuple{std::optional<std::string>{"mail@example.test"}, 1});
    ASSERT_EQ(present.size(), 2);
    EXPECT_EQ(std::get<std::string>(present[0].value->get()), "mail@example.test");
    EXPECT_FALSE(present[0].nullType.has_value());
    const auto absent = ast::collectParameters(patch, std::tuple{std::optional<std::string>{}, 1});
    EXPECT_FALSE(absent[0].value.has_value());
    EXPECT_EQ(absent[0].nullType, orm::model::ColumnType::String);
    constexpr auto nullPointer = update<User>().set(col<&User::email>(), nullptr).where(col<&User::id>() == 1);
    constexpr auto nullOptional = update<User>().set(col<&User::email>(), std::nullopt).where(col<&User::id>() == 1);
    EXPECT_FALSE(ast::collectParameters(nullPointer, std::tuple{})[0].value.has_value());
    EXPECT_FALSE(ast::collectParameters(nullOptional, std::tuple{})[0].value.has_value());
}

TEST(CompiledSqlCoverageTest, builtinPolicyValidatesNamesAndRuntimePolicyRejectsBoundPagination)
{
    const sql::StaticSqlPolicy<Flavor::SQLite> sqlite;
    const sql::StaticSqlPolicy<Flavor::PostgreSQL> postgres;
    EXPECT_EQ(sqlite.quoteIdentifier("quoted\"name"), "\"quoted\"\"name\"");
    EXPECT_EQ(postgres.quoteIdentifier("plain"), "\"plain\"");
    EXPECT_EQ(sqlite.bindMarker("orm_p12"), ":orm_p12");
    EXPECT_EQ(postgres.bindMarker("orm_p0"), ":orm_p0");
    EXPECT_THROW((void)sqlite.bindMarker("not-portable"), std::invalid_argument);
    EXPECT_THROW((void)postgres.bindMarker(""), std::invalid_argument);
    EXPECT_THROW((void)sqlite.quoteIdentifier(""), std::invalid_argument);
    EXPECT_THROW((void)postgres.quoteIdentifier(std::string(64, 'a')), std::invalid_argument);
    sql::SqlProgram program;
    program.boundPagination = true;
    const orm::db::sqlite::SqliteDialect dialect;
    EXPECT_THROW((void)sql::RuntimeSqlPolicy{dialect}.pagination(program.view()), std::logic_error);
}

TEST(CompiledSqlCoverageTest, preflightPolicyRecordsInvalidIdentifiersWithoutThrowing)
{
    bool accepted = true;
    const ast::compiled::CheckingPolicy<Flavor::PostgreSQL> postgres{{}, &accepted};
    EXPECT_EQ(postgres.quoteIdentifier("quoted\"name"), "\"quoted\"\"name\"");
    EXPECT_TRUE(accepted);
    EXPECT_EQ(postgres.quoteIdentifier(""), "\"\"");
    EXPECT_FALSE(accepted);
    accepted = true;
    (void)postgres.quoteIdentifier(std::string_view{"embedded\0nul", 12});
    EXPECT_FALSE(accepted);
    accepted = true;
    (void)postgres.quoteIdentifier(std::string(64, 'a'));
    EXPECT_FALSE(accepted);
    accepted = true;
    const ast::compiled::CheckingPolicy<Flavor::SQLite> sqlite{{}, &accepted};
    EXPECT_EQ(sqlite.quoteIdentifier(std::string(64, 'a')).size(), 66);
    EXPECT_TRUE(accepted);
}

TEST(CompiledSqlCoverageTest, sharedPathParserPreservesDiagnosticsAndRendererAliasContext)
{
    EXPECT_TRUE(sql::sqlPath(sql::SqlSource{}).empty());
    EXPECT_EQ(sql::sqlPath(sql::parseSqlSource("profile.city")), "profile.city");
    for (const auto path : {"", ".id", "profile.", "profile..city", "profile.city.id"})
        EXPECT_THROW((void)sql::parseSqlSource(path), std::invalid_argument);
    const auto model = orm::modelView<Schema, User>();
    auto malformed = sql::parseSqlSource("id");
    malformed.pathSize = 3;
    EXPECT_THROW((void)sql::resolveSqlColumn(malformed, model), std::invalid_argument);
    const orm::db::sqlite::SqliteDialect dialect;
    orm::db::commands::RenderContext context{.model = model, .dialect = dialect};
    context.relationAliases.emplace("profile", "selected_profile");
    EXPECT_EQ(orm::db::commands::renderColumn(ast::Column{"profile.city"}, context), "\"selected_profile\".\"city\"");
    EXPECT_TRUE(orm::db::commands::renderWhere(std::optional<ast::Predicate>{}, context).empty());
    const std::optional<ast::Predicate> predicate{ast::Column{"age"} > 18};
    EXPECT_EQ(orm::db::commands::renderWhere(predicate, context), " WHERE \"static_plan_users\".\"age\" > :orm_p0");
}

TEST(CompiledSqlCoverageTest, sharedEmitterRejectsMalformedCollectionMappings)
{
    constexpr auto authorPlan = select<collection_models::Author>().where(exists<&collection_models::Author::books>());
    const auto authorProgram = ast::compiled::program<std::remove_cvref_t<decltype(authorPlan)>>();
    MutableSchema authors{collection_models::Schema::view};
    const auto authorIndex = orm::modelView<collection_models::Schema, collection_models::Author>().modelIndex;
    authors.relations[authorIndex][0].mappedBy = "missing_relation";
    EXPECT_THROW((void)sql::emitSql(authorProgram.view(), authors.schema.at(authorIndex),
                                    sql::StaticSqlPolicy<Flavor::SQLite>{}),
                 std::invalid_argument);

    constexpr auto userPlan = select<User>().where(exists<&User::roles>());
    const auto userProgram = ast::compiled::program<std::remove_cvref_t<decltype(userPlan)>>();
    MutableSchema users{Schema::view};
    const auto userIndex = orm::modelView<Schema, User>().modelIndex;
    auto& roles = users.relations[userIndex].back();
    roles.junction = {};
    EXPECT_THROW(
        (void)sql::emitSql(userProgram.view(), users.schema.at(userIndex), sql::StaticSqlPolicy<Flavor::SQLite>{}),
        std::invalid_argument);
    const std::array<std::string_view, 2> mismatched{"user_id", "other_id"};
    roles.junction = Schema::view.at(userIndex)->relations.back().junction;
    roles.junction.ownerColumns = mismatched;
    EXPECT_THROW(
        (void)sql::emitSql(userProgram.view(), users.schema.at(userIndex), sql::StaticSqlPolicy<Flavor::SQLite>{}),
        std::invalid_argument);
}

TEST(CompiledSqlCoverageTest, sharedEmitterDefendsAggregateOperatorsAndIncompleteUpdates)
{
    constexpr auto aggregate = select<User>().having(countAll<User>() > 0);
    auto program = ast::compiled::program<std::remove_cvref_t<decltype(aggregate)>>();
    program.nodes[program.having].operation = static_cast<unsigned>(ast::ComparisonOperator::Like);
    EXPECT_THROW(
        (void)sql::emitSql(program.view(), orm::modelView<Schema, User>(), sql::StaticSqlPolicy<Flavor::SQLite>{}),
        std::invalid_argument);
    program = {};
    program.operation = sql::SqlOperation::Update;
    EXPECT_THROW(
        (void)sql::emitSql(program.view(), orm::modelView<Schema, User>(), sql::StaticSqlPolicy<Flavor::SQLite>{}),
        std::invalid_argument);
    program.assignments.push_back({sql::parseSqlSource("age"), 0});
    EXPECT_THROW(
        (void)sql::emitSql(program.view(), orm::modelView<Schema, User>(), sql::StaticSqlPolicy<Flavor::SQLite>{}),
        std::invalid_argument);
}
} // namespace
