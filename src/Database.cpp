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

auto Database::addColumnsForForeignIds(const model::ForeignIdsInfo& foreignIdsInfo, const model::ColumnInfo& columnInfo)
    -> std::string
{
    if (not foreignIdsInfo.contains(columnInfo.name))
    {
        throw std::runtime_error("No foreign ids info for column " + columnInfo.name);
    }

    std::string query{};

    const auto& fieldName = columnInfo.name;
    const auto isNullable = columnInfo.isNotNull ? " NOT NULL" : "";

    for (const auto& [foreignIdName, foreignIColumnInfo] : foreignIdsInfo.at(fieldName).idFields)
    {
        query.append(std::format("{}_{} {}{},\n", fieldName, foreignIdName,
                                 typeTranslatorFactory.getTranslator(backendType)->toSqlType(foreignIColumnInfo.type),
                                 isNullable));
    }

    return query;
}

auto Database::addForeignIds(const model::ForeignIdsInfo& foreignIdsInfo) -> std::string
{
    std::string query{};

    for (const auto& [fieldName, foreignIds] : foreignIdsInfo)
    {
        std::string foreignKeyQuery{"FOREIGN KEY("};
        for (const auto& [foreignIdName, foreignIColumnInfo] : foreignIds.idFields)
        {
            foreignKeyQuery.append(std::format("{}_{}, ", fieldName, foreignIdName));
        }

        foreignKeyQuery.pop_back();
        foreignKeyQuery.pop_back();

        foreignKeyQuery.append(std::format(") REFERENCES {}(", foreignIds.tableName));

        for (const auto& [foreignIdName, foreignIColumnInfo] : foreignIds.idFields)
        {
            foreignKeyQuery.append(std::format("{}, ", foreignIdName));
        }

        foreignKeyQuery.pop_back();
        foreignKeyQuery.pop_back();

        foreignKeyQuery.append("),\n");

        query.append(foreignKeyQuery);
    }

    if (not query.empty())
    {
        query.pop_back();
        query.pop_back();
    }

    return query;
}
}
