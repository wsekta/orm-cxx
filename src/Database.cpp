#include "orm-cxx/database.hpp"

#include <regex>

#include "soci/empty/soci-empty.h"

namespace
{
const std::regex sqliteRegex("sqlite3\\:\\/\\/.*");
}

namespace orm
{
Database::Database() : sql(), backendType(db::BackendType::Empty), typeTranslatorFactory() {}

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
}
