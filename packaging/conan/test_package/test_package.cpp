#include <concepts>
#include <orm-cxx/database.hpp>
#include <orm-cxx/projection_query.hpp>
#include <orm-cxx/update.hpp>
#include <string>

#include "BackendExpectations.hpp"

template <typename Owner>
consteval auto sqliteRelationOverloadsMatchConfiguration() -> bool
{
    constexpr bool create = requires(Owner owner) {
        { orm::db::relations::createTableStatements(owner) } -> std::same_as<std::vector<std::string>>;
    };
    constexpr bool drop = requires(Owner owner) {
        { orm::db::relations::dropTableStatements(owner) } -> std::same_as<std::vector<std::string>>;
    };
    constexpr bool link =
        requires(Owner owner, orm::model::RelationView relation, const orm::db::binding::PrimaryKey& key) {
            { orm::db::relations::linkStatement(owner, relation, key, key) } -> std::same_as<orm::db::Statement>;
        };
    constexpr bool unlink =
        requires(Owner owner, orm::model::RelationView relation, const orm::db::binding::PrimaryKey& key) {
            { orm::db::relations::unlinkStatement(owner, relation, key, key) } -> std::same_as<orm::db::Statement>;
        };
    constexpr bool select = requires(Owner owner, orm::model::RelationView relation, std::string sql,
                                     const std::vector<orm::db::binding::PrimaryKey>& keys) {
        {
            orm::db::relations::collectionSelectStatement(owner, relation, sql, keys, true)
        } -> std::same_as<orm::db::Statement>;
    };
    constexpr bool enabled = orm::config::sqliteBackendEnabled;
    return create == enabled and drop == enabled and link == enabled and unlink == enabled and select == enabled;
}

static_assert(sqliteRelationOverloadsMatchConfiguration<orm::model::ModelView>());

static_assert(not orm::test::config::expectedSqliteBackend.has_value() or
              orm::config::sqliteBackendEnabled == *orm::test::config::expectedSqliteBackend);
static_assert(not orm::test::config::expectedPostgresqlBackend.has_value() or
              orm::config::postgresqlBackendEnabled == *orm::test::config::expectedPostgresqlBackend);

namespace package_models
{
struct Entry
{
    int id;
    std::string name;
};
struct EntrySummary
{
    std::string name;
};
using Schema = orm::Schema<Entry>;
} // namespace package_models

int main()
{
    using orm::query::col;

    // Compile and build the typed API even when SQLite is disabled.
    orm::Query<package_models::Entry> query;
    query.where(col<&package_models::Entry::id>() == short{1})
        .orderBy(orm::query::asc(col<&package_models::Entry::id>()));
    orm::Update<package_models::Entry> update;
    update.set(col<&package_models::Entry::name>(), "updated dependencies")
        .where(col<&package_models::Entry::id>() == 1);
    orm::ProjectionQuery<package_models::Entry, package_models::EntrySummary> projection;
    projection.project(orm::query::as("name", col<&package_models::Entry::name>()))
        .where(col<&package_models::Entry::id>() == 1);

    using namespace orm::query;
    constexpr auto selectPlan =
        select<package_models::Entry>().where(col<&package_models::Entry::id>() == param<int, 0>());
    constexpr auto updatePlan = orm::query::update<package_models::Entry>()
                                    .set(col<&package_models::Entry::name>(), param<std::string, 0>())
                                    .where(col<&package_models::Entry::id>() == param<int, 1>());
    constexpr auto projectionPlan =
        selectAs<package_models::Entry, package_models::EntrySummary>(as<"name">(col<&package_models::Entry::name>()))
            .where(col<&package_models::Entry::id>() == param<int, 0>());
    constexpr auto removePlan =
        remove<package_models::Entry>().where(col<&package_models::Entry::id>() == param<int, 0>());
    (void)selectPlan.toDynamic(1);
    (void)updatePlan.toDynamic(std::string{"updated dependencies"}, 1);
    (void)projectionPlan.toDynamic(1);
    (void)removePlan.toDynamic(1);

    orm::Database<package_models::Schema> database;
    const orm::db::CommandGeneratorFactory factory;
    const auto* sqlite = factory.findBackend("sqlite3://:memory:");
    const auto* postgresql = factory.findBackend("postgresql://host=localhost dbname=orm_cxx");

    if ((sqlite != nullptr) != orm::config::sqliteBackendEnabled)
    {
        return 1;
    }
    if ((postgresql != nullptr) != orm::config::postgresqlBackendEnabled)
    {
        return 2;
    }

    if constexpr (orm::config::sqliteBackendEnabled)
    {
        database.connect("sqlite3://:memory:");
        database.createTable<package_models::Entry>();
        database.insert(package_models::Entry{1, "packaged dependencies"});
        const auto entries = database.select(selectPlan, 1);
        if (entries.size() != 1 || entries.front().id != 1 || entries.front().name != "packaged dependencies")
        {
            return 3;
        }
        if (database.update(updatePlan, std::string{"updated dependencies"}, 1) != 1)
        {
            return 4;
        }
        const auto summaries = database.select(projectionPlan, 1);
        if (summaries.size() != 1 || summaries.front().name != "updated dependencies")
        {
            return 5;
        }
        if (database.remove(removePlan, 1) != 1 || !database.select(query).empty())
        {
            return 6;
        }
        database.disconnect();
    }
    return 0;
}
