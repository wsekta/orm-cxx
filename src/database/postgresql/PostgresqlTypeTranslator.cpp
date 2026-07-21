#include "PostgresqlTypeTranslator.hpp"

#include <stdexcept>

namespace orm::db::postgresql
{
auto PostgresqlTypeTranslator::toSqlType(model::ColumnType type) const -> std::string
{
    switch (type)
    {
    case model::ColumnType::Bool:
        return "BOOLEAN";

    case model::ColumnType::Char:
    case model::ColumnType::UnsignedChar:
    case model::ColumnType::Short:
        return "SMALLINT";

    case model::ColumnType::UnsignedShort:
    case model::ColumnType::Int:
        return "INTEGER";

    case model::ColumnType::UnsignedInt:
    case model::ColumnType::LongLong:
    case model::ColumnType::UnsignedLongLong:
        return "BIGINT";

    case model::ColumnType::Float:
    case model::ColumnType::Double:
        return "DOUBLE PRECISION";

    case model::ColumnType::String:
        return "TEXT";

    case model::ColumnType::Uuid:
    case model::ColumnType::Unknown:
    case model::ColumnType::OneToOne:
        break;
    }

    throw std::runtime_error{"Unsupported type by PostgreSQL " + orm::model::toString(type)};
}
} // namespace orm::db::postgresql
