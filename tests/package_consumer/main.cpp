#include <concepts>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "BackendExpectations.hpp"
import orm;

template <typename Owner>
consteval auto sqliteRelationOverloadsMatchConfiguration() -> bool
{
    constexpr bool create = requires(Owner owner) {
        { orm::db::relations::createTableStatements(owner) } -> std::same_as<std::vector<std::string>>;
    };
    constexpr bool drop = requires(Owner owner) {
        { orm::db::relations::dropTableStatements(owner) } -> std::same_as<std::vector<std::string>>;
    };
    constexpr bool link = requires(Owner owner, orm::model::RelationView relation, const orm::db::PrimaryKey& key) {
        { orm::db::relations::linkStatement(owner, relation, key, key) } -> std::same_as<orm::db::Statement>;
    };
    constexpr bool unlink = requires(Owner owner, orm::model::RelationView relation, const orm::db::PrimaryKey& key) {
        { orm::db::relations::unlinkStatement(owner, relation, key, key) } -> std::same_as<orm::db::Statement>;
    };
    constexpr bool select = requires(Owner owner, orm::model::RelationView relation, std::string sql,
                                     const std::vector<orm::db::PrimaryKey>& keys) {
        {
            orm::db::relations::collectionSelectStatement(owner, relation, sql, keys, true)
        } -> std::same_as<orm::db::Statement>;
    };
    constexpr bool enabled = orm::config::sqliteBackendEnabled;
    return create == enabled and drop == enabled and link == enabled and unlink == enabled and select == enabled;
}

static_assert(sqliteRelationOverloadsMatchConfiguration<orm::model::ModelView>());

struct PackageModel
{
    int id;
    std::string name;
};

struct PackageSummary
{
    std::string name;
};

using PackageSchema = orm::Schema<PackageModel>;

static_assert(orm::reflection::fieldCount<PackageModel> == 2);
static_assert(orm::reflection::fieldName<PackageModel, 0>() == "id");

static_assert(not orm::test::config::expectedSqliteBackend.has_value() or
              orm::config::sqliteBackendEnabled == *orm::test::config::expectedSqliteBackend);
static_assert(not orm::test::config::expectedPostgresqlBackend.has_value() or
              orm::config::postgresqlBackendEnabled == *orm::test::config::expectedPostgresqlBackend);

int main()
{
    try
    {
        using orm::query::col;

        // These builders are exercised for every installed backend configuration.
        orm::Query<PackageModel> query;
        query.where(col<&PackageModel::id>() == short{1}).orderBy(orm::query::asc(col<&PackageModel::id>()));
        orm::Update<PackageModel> update;
        update.set(col<&PackageModel::name>(), "updated").where(col<&PackageModel::id>() == 1);
        orm::ProjectionQuery<PackageModel, PackageSummary> projection;
        projection.project(orm::query::as("name", col<&PackageModel::name>())).where(col<&PackageModel::id>() == 1);

        using namespace orm::query;
        constexpr auto selectPlan = select<PackageModel>().where(col<&PackageModel::id>() == param<int, 0>());
        constexpr auto updatePlan = orm::query::update<PackageModel>()
                                        .set(col<&PackageModel::name>(), param<std::string, 0>())
                                        .where(col<&PackageModel::id>() == param<int, 1>());
        constexpr auto projectionPlan = selectAs<PackageModel, PackageSummary>(as<"name">(col<&PackageModel::name>()))
                                            .where(col<&PackageModel::id>() == param<int, 0>());
        constexpr auto removePlan = remove<PackageModel>().where(col<&PackageModel::id>() == param<int, 0>());
        (void)selectPlan.toDynamic(1);
        (void)updatePlan.toDynamic(std::string{"updated"}, 1);
        (void)projectionPlan.toDynamic(1);
        (void)removePlan.toDynamic(1);

        orm::Database database;
        auto context = database.orm<PackageSchema>();
        const orm::db::CommandGeneratorFactory factory;
        const auto* sqlite = factory.findBackend("sqlite3://:memory:");
        const auto* postgresql = factory.findBackend("postgresql://host=localhost dbname=orm_cxx");
        if ((sqlite != nullptr) != orm::config::sqliteBackendEnabled or
            (postgresql != nullptr) != orm::config::postgresqlBackendEnabled)
        {
            std::cerr << "Backend registration does not match the installed package features\n";
            return 1;
        }
        if (postgresql != nullptr and postgresql->type() != orm::db::BackendType::Postgres)
        {
            return 2;
        }

        if constexpr (orm::config::sqliteBackendEnabled)
        {
            database.connect("sqlite3://:memory:");
            context.createTable<PackageModel>();
            context.insert(std::vector<PackageModel>{{1, "first"}, {2, "second"}});

            auto rows = context.select(selectPlan, 1);
            if (rows.size() != 1 or rows.front().name != "first")
            {
                return 3;
            }

            if (context.update(updatePlan, std::string{"updated"}, 1) != 1)
            {
                return 4;
            }
            rows = context.select(query);
            if (rows.size() != 1 or rows.front().name != "updated")
            {
                return 5;
            }
            const auto summaries = context.select(projectionPlan, 1);
            if (summaries.size() != 1 or summaries.front().name != "updated")
            {
                return 8;
            }
            if (context.remove(removePlan, 1) != 1 or not context.select(query).empty())
            {
                return 6;
            }
            context.deleteTable<PackageModel>();
            database.disconnect();
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 7;
    }
}
