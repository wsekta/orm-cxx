#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "orm-cxx/database.hpp"
#include "orm-cxx/query.hpp"
#include "orm-cxx/update.hpp"

struct PackageModel
{
    int id;
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
        using orm::query::col;
        orm::Database<PackageSchema> database;
        database.connect("sqlite3://:memory:");
        database.createTable<PackageModel>();
        database.insert(std::vector<PackageModel>{{1, "first"}, {2, "second"}});

        orm::Query<PackageModel> query;
        query.where(col("id") == 1);
        auto rows = database.select(query);
        if (rows.size() != 1 or rows.front().name != "first")
        {
            return 3;
        }

        orm::Update<PackageModel> update;
        update.set(col("name"), "updated").where(col("id") == 1);
        if (database.update(update) != 1)
        {
            return 4;
        }
        rows = database.select(query);
        if (rows.size() != 1 or rows.front().name != "updated")
        {
            return 5;
        }
        if (database.remove<PackageModel>(col("id") == 1) != 1 or not database.select(query).empty())
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
