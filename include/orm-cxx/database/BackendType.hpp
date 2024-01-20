#pragma once

namespace orm::db
{
enum class BackendType : char
{
    Sqlite,
    Postgres [[maybe_unused]],
    Mysql [[maybe_unused]],
    Oracle [[maybe_unused]],
    Firebird [[maybe_unused]],
    Db2 [[maybe_unused]],
    Odbc [[maybe_unused]],
    Empty
};
} // namespace orm::db
