#include "SqliteTypeTranslator.hpp"

#include <regex>
#include <stdexcept>

namespace
{
const std::regex stringRegex{R"((class )?std.*::basic_string<.*>\W?)"};
}

namespace orm::db::sqlite
{
auto SqliteTypeTranslator::toSqlType(model::ColumnType type) const -> std::string
{
    switch (type)
    {
    case model::ColumnType::Int:
        return "INTEGER";

    case model::ColumnType::Float:
    case model::ColumnType::Double:
        return "REAL";

    case model::ColumnType::String:
        return "TEXT";

    default:
        throw std::runtime_error("Unsupported type by sqlite");
    }
}
} // namespace orm::db::sqlite
