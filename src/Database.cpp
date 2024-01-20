#include "orm-cxx/database.hpp"

#include <format>
#include <regex>

#include "soci/empty/soci-empty.h"

namespace
{
const std::regex sqliteRegex("sqlite3\\:\\/\\/.*");
}

namespace orm
{
Database::Database() : sql(), backendType(db::BackendType::Empty), commandGeneratorFactory{} {}

auto Database::connect(const std::string& connectionString) -> void
{
    if (std::regex_match(connectionString, sqliteRegex))
    {
        backendType = db::BackendType::Sqlite;
    }

    sql.open(connectionString);
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
}
