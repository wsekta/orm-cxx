#pragma once

namespace orm::db
{
enum class BackendType
{
    Sqlite,
    Postgres,
    Mysql,
    Oracle,
    Firebird,
    Db2,
    Odbc,
    Empty
};
}