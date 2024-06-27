#include "orm-cxx/database.hpp"

#include <regex>

namespace
{
const std::regex sqliteRegex(R"(sqlite3\:\/\/.*)");
}

namespace orm
{
Database::Database() : backendType{db::BackendType::Empty} {}

auto Database::connect(const std::string& connectionString) -> void
{
    if (std::regex_match(connectionString, sqliteRegex))
    {
        backendType = db::BackendType::Sqlite;
    }

    sql.open(connectionString);
}

auto Database::disconnect() -> void
{
    backendType = db::BackendType::Empty;

    sql.close();
}

auto Database::getBackendType() -> db::BackendType
{
    return backendType;
}

auto Database::beginTransaction() -> void
{
    transaction = std::make_unique<soci::transaction>(sql);
}

auto Database::commitTransaction() -> void
{
    transaction->commit();
    transaction.reset();
}

auto Database::rollbackTransaction() -> void
{
    transaction->rollback();
    transaction.reset();
}
} // namespace orm
