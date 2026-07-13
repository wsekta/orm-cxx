# Backends

`orm-cxx` currently supports SQLite. Other backend enum values are placeholders
for future implementation and are not evidence of working database support.

## Support status

| Backend | Status | Verification |
| --- | --- | --- |
| SQLite | Supported | Integration tests on GCC, Clang, and MSVC; Clang coverage |
| PostgreSQL | Planned | Not currently available |
| MySQL | Future | Not currently available |
| ODBC | Future | Not currently available |
| Oracle | Future | Not currently available |
| Firebird | Future | Not currently available |
| DB2 | Future | Not currently available |

The project will list a backend as **supported** only after it passes the common
conformance profile against a real database in CI. An implementation under
development is **experimental** and must publish its known capability gaps. A
name in `BackendType`, a SOCI driver, or SQL-rendering unit tests alone do not
qualify as support.

## SQLite

Connect with a SOCI SQLite connection string:

```cpp
orm::Database database;
database.connect("sqlite3://application.db");
```

Applications that already know the backend may select it explicitly and inspect
the negotiated capability profile:

```cpp
orm::Database database;
database.connect(orm::db::BackendType::Sqlite, "sqlite3://application.db");

const auto& capabilities = database.getBackendCapabilities();
if (capabilities.query.groupBy)
{
    // The typed GROUP BY API is available for this connection.
}
```

Failures that cross the backend boundary use `orm::DatabaseError`. Its code,
backend type, logical operation, and optional native code are stable structured
fields; diagnostic messages never include connection strings or bound values.

SQLite is the reference backend for the current public model, query, mutation,
transaction, and relation behavior. Its session enables foreign-key enforcement
for ORM-managed connections.

SQLite supports the current scalar and nullable model fields, manual and
composite primary keys, SQLite integer auto-increment keys, full-model and
projection queries, affected-row reporting, transactions, to-one relations, and
the documented collection relations.

SQLite's SOCI transport exchanges integers as signed 64-bit values. Unsigned
64-bit fields therefore accept values through `INT64_MAX`; larger input is
reported as `DatabaseErrorCode::Conversion` rather than wrapping. This bound is
available as `getBackendCapabilities().valueLimits.maxUnsignedLongLong`.

SQLite does not have native UUID, date, or time column types. Those types are not
part of the current portable model contract. Converter semantics will be defined
only after a backend with meaningful native types is implemented.

## Build configuration

The SQLite implementation is controlled by the `ORM_CXX_ENABLE_SQLITE_BACKEND`
CMake option and is enabled by default. The default target name and alias remain
`orm-cxx` and `orm-cxx::orm-cxx`.

For a backend-neutral consumer build that links only the shared SOCI core, use:

```sh
cmake -S . -B build/core-only \
  -DORM_CXX_ENABLE_SQLITE_BACKEND=OFF \
  -DORM_CXX_BUILD_TESTS=OFF \
  -DORM_CXX_BUILD_EXAMPLES=OFF
```

With SQLite disabled, its implementation sources, `soci_sqlite3` link dependency,
and factory registration are omitted; `soci_core` remains required. The current
test suite and examples exercise SQLite, so requesting either developer target
while disabling SQLite is a configuration error rather than silently producing
an incomplete target. Parent-project consumers default tests and examples to
`OFF`, so they can disable SQLite with only the backend option.

## Capability reporting

Backend capabilities describe user-visible behavior, while dialect code hides
SQL syntax differences. Applications should not branch on a backend enum merely
to choose pagination, quoting, upsert, or DDL syntax.

When an optional feature is unavailable, the backend contract requires an
explicit unsupported-feature error before SQL execution. Supported behavior must
pass the corresponding common conformance tests. Numeric limits, including the
maximum number of bind parameters in a statement, are backend data and must not
be hard-coded as SQLite assumptions in shared code.

## Raw SQL portability

Raw query and ordering fragments use the selected database's SQL dialect. A raw
fragment written for SQLite may not work with a future PostgreSQL or MySQL
backend. Explicit raw-fragment values remain prepared bind parameters.

Use the typed query API for portable application logic and isolate intentional
raw SQL behind backend-specific application code.

See [Backend portability](backend-portability.md) for the shared behavior and CI
policy, and [Backend extension contract](backend-extension.md) for contributor
requirements.
