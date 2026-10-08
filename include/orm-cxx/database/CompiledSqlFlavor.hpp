#pragma once

namespace orm::db
{
enum class CompiledSqlFlavor
{
    None,
    SQLite,
    PostgreSQL,
};
} // namespace orm::db
