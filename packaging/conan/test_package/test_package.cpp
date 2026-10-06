#include <orm-cxx/database.hpp>
#include <orm-cxx/projection_query.hpp>
#include <orm-cxx/update.hpp>
#include <string>

#if !defined(ORM_CXX_ENABLE_SQLITE_BACKEND) || !defined(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
#error The package must propagate its configured backends to consumers.
#endif

static_assert(ORM_CXX_ENABLE_SQLITE_BACKEND == ORM_CXX_EXPECT_SQLITE);
static_assert(ORM_CXX_ENABLE_POSTGRESQL_BACKEND == ORM_CXX_EXPECT_POSTGRESQL);

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

    orm::Database<package_models::Schema> database;
    const orm::db::CommandGeneratorFactory factory;
    const auto* sqlite = factory.findBackend("sqlite3://:memory:");
    const auto* postgresql = factory.findBackend("postgresql://host=localhost dbname=orm_cxx");

    if ((sqlite != nullptr) != static_cast<bool>(ORM_CXX_ENABLE_SQLITE_BACKEND))
    {
        return 1;
    }
    if ((postgresql != nullptr) != static_cast<bool>(ORM_CXX_ENABLE_POSTGRESQL_BACKEND))
    {
        return 2;
    }

#if ORM_CXX_ENABLE_SQLITE_BACKEND
    database.connect("sqlite3://:memory:");
    database.createTable<package_models::Entry>();
    database.insert(package_models::Entry{1, "packaged dependencies"});
    const auto entries = database.select(query);
    if (entries.size() != 1 || entries.front().id != 1 || entries.front().name != "packaged dependencies")
    {
        return 3;
    }
    if (database.update(update) != 1)
    {
        return 4;
    }
    const auto summaries = database.select(projection);
    if (summaries.size() != 1 || summaries.front().name != "updated dependencies")
    {
        return 5;
    }
    database.disconnect();
#endif
    return 0;
}
