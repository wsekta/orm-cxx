#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "orm-cxx/database.hpp"
#include "orm-cxx/projection_query.hpp"
#include "orm-cxx/query.hpp"
#include "orm-cxx/update.hpp"

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

#ifdef ORM_CXX_EXPECT_SQLITE
static_assert(ORM_CXX_ENABLE_SQLITE_BACKEND == ORM_CXX_EXPECT_SQLITE);
#endif
#ifdef ORM_CXX_EXPECT_POSTGRESQL
static_assert(ORM_CXX_ENABLE_POSTGRESQL_BACKEND == ORM_CXX_EXPECT_POSTGRESQL);
#endif

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

        orm::Database<PackageSchema> database;
        const orm::db::CommandGeneratorFactory factory;
        const auto* sqlite = factory.findBackend("sqlite3://:memory:");
        const auto* postgresql = factory.findBackend("postgresql://host=localhost dbname=orm_cxx");
        if ((sqlite != nullptr) != static_cast<bool>(ORM_CXX_ENABLE_SQLITE_BACKEND) or
            (postgresql != nullptr) != static_cast<bool>(ORM_CXX_ENABLE_POSTGRESQL_BACKEND))
        {
            std::cerr << "Backend registration does not match the installed package features\n";
            return 1;
        }
        if (postgresql != nullptr and postgresql->type() != orm::db::BackendType::Postgres)
        {
            return 2;
        }

#if ORM_CXX_ENABLE_SQLITE_BACKEND
        database.connect("sqlite3://:memory:");
        database.createTable<PackageModel>();
        database.insert(std::vector<PackageModel>{{1, "first"}, {2, "second"}});

        auto rows = database.select(query);
        if (rows.size() != 1 or rows.front().name != "first")
        {
            return 3;
        }

        if (database.update(update) != 1)
        {
            return 4;
        }
        rows = database.select(query);
        if (rows.size() != 1 or rows.front().name != "updated")
        {
            return 5;
        }
        const auto summaries = database.select(projection);
        if (summaries.size() != 1 or summaries.front().name != "updated")
        {
            return 8;
        }
        if (database.remove<PackageModel>(col<&PackageModel::id>() == 1) != 1 or not database.select(query).empty())
        {
            return 6;
        }
        database.deleteTable<PackageModel>();
        database.disconnect();
#endif
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 7;
    }
}
