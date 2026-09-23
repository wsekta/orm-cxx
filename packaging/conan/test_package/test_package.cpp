#include <orm-cxx/database.hpp>
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
using Schema = orm::Schema<Entry>;
} // namespace package_models

int main()
{
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
    orm::Query<package_models::Entry> query;
    const auto entries = database.select(query);
    if (entries.size() != 1 || entries.front().id != 1 || entries.front().name != "packaged dependencies")
    {
        return 3;
    }
    database.disconnect();
#endif
    return 0;
}
