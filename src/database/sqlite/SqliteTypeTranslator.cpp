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
    case model::ColumnType::Bool:
        return "BOOLEAN";

    case model::ColumnType::Char:
    case model::ColumnType::UnsignedChar:
        return "TINYINT";

    case model::ColumnType::Short:
    case model::ColumnType::UnsignedShort:
        return "SMALLINT";

    case model::ColumnType::Int:
        return "INTEGER";

    case model::ColumnType::LongLong:
        return "BIGINT";

    case model::ColumnType::UnsignedInt:
    case model::ColumnType::UnsignedLongLong:
        return "UNSIGNED BIG INT";

    case model::ColumnType::Float:
    case model::ColumnType::Double:
        return "REAL";

    case model::ColumnType::String:
        return "TEXT";

    default:
        throw std::runtime_error("Unsupported type by sqlite " + orm::model::toString(type));
    }
}
} // namespace orm::db::sqlite
