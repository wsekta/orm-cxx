# Backends

`orm-cxx` supports SQLite by default and an optional PostgreSQL adapter. Other
backend enum values remain placeholders and are not evidence of working
database support.

## Support status

| Backend | Status | Verification |
| --- | --- | --- |
| SQLite | Supported | Integration tests on GCC, Clang, and MSVC; Clang coverage |
| PostgreSQL | Supported, opt-in | Live common conformance on PostgreSQL 15 and 18; GCC and Clang; Clang coverage; MSVC compile/link |
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

## Compatibility at a glance

SQLite and PostgreSQL are first-class supported backends for the shared ORM
contract. Supported does not mean that their native SQL semantics or every
optional capability are identical. Applications can use the same model, CRUD,
projection, transaction, and relation APIs; capability reporting exposes the
intentional differences before SQL execution.

| Contract area | SQLite | PostgreSQL |
| --- | --- | --- |
| Build mode | Default backend | Opt-in backend |
| Scalar and nullable model fields | All currently documented portable fields | All currently documented portable fields |
| Manual, composite, and generated integer keys | Supported | Supported |
| CRUD and exact affected-row counts | Supported | Supported |
| Projection and aggregate projection queries | Supported | Supported |
| Full-model `GROUP BY` / `HAVING` | Supported with SQLite representative-row semantics | Rejected; use a grouped projection |
| To-one, one-to-many, and many-to-many relations | Supported | Supported |
| Foreign keys and junction-row cascade | Enabled per ORM session | Native PostgreSQL enforcement |
| Explicit transactions | Supported | Supported; a statement error requires rollback |
| ORM bind-parameter ceiling | 900 | 65,535 |
| ORM identifier restriction | No additional byte limit; bind-backed physical columns use portable ASCII names | 63 bytes; bind-backed physical columns use portable ASCII names |
| Default `LIKE` behavior | ASCII case-insensitive | Case-sensitive |
| Native NULL ordering | `ASC`: first; `DESC`: last | `ASC`: last; `DESC`: first |
| Namespace selection | SQLite database connection | Active PostgreSQL `search_path` |

Raw SQL remains backend-specific. Code that needs portable behavior should use
the typed API and inspect `getBackendCapabilities()` for optional operations.
For both bundled adapters, a physical non-identity column name used as a SOCI
bind name may contain only ASCII letters, digits, and `_`.
Connection strings and identifiers containing an embedded NUL byte are rejected
before they reach either database driver's C API.

## SQLite

Connect with a SOCI SQLite connection string:

```cpp
orm::Database<orm::Schema<>> database;  // empty schema suffices for connection probing
database.connect("sqlite3://application.db");
```

Applications that already know the backend may select it explicitly and inspect
the negotiated capability profile:

```cpp
orm::Database<orm::Schema<>> database;
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

The ORM uses a conservative SQLite ceiling of 900 bind parameters per statement
and batches collection loading accordingly. The runtime reports this value
through `BackendRuntimeLimits`; applications should not hard-code it.

SQLite does not have native UUID, date, or time column types. Those types are not
part of the current portable model contract. Converter semantics will be defined
only after a backend with meaningful native types is implemented.

## PostgreSQL

Enable the adapter at configure time and connect with a `postgresql://`
selector followed by SOCI's keyword/value connection payload:

```sh
cmake -S . -B build/postgresql \
  -DORM_CXX_ENABLE_POSTGRESQL_BACKEND=ON
```

```cpp
orm::Database<orm::Schema<>> database;
database.connect(
    "postgresql://host=localhost port=5432 dbname=application user=application password=secret");
```

The payload is passed to the SOCI PostgreSQL backend. It is not the RFC-style
`postgresql://user@host/database` URI syntax. Passwords and complete connection
strings are excluded from ORM diagnostics. The vendored SOCI parser cannot
preserve whitespace inside keyword values, so connection values must not contain
spaces. Prefer a PostgreSQL passfile/`PGPASSFILE` instead of embedding a complex
password in the DSN.

With examples enabled, the `postgresql-example` target reads
`ORM_CXX_POSTGRESQL_EXAMPLE_DSN`, connects, reports selected capabilities, and
disconnects without creating database objects. It is also built by the
PostgreSQL CI profiles.

The supported server range is PostgreSQL 15 through 18. CI verifies the lower
boundary with GCC 13 and the upper boundary with Clang 18 and coverage. MSVC
builds and links the adapter and every consumer configuration; live server
behavior is verified on Linux.

PostgreSQL supports the current scalar and nullable fields, manual and composite
keys, generated integer identity keys, exact affected-row counts, transactions,
full and projection selects, collection predicates/includes, to-one,
one-to-many, and many-to-many relations. `INSERT ... ON CONFLICT DO NOTHING`
provides idempotent relation linking.

There are deliberate v1 exclusions:

- full-model `GROUP BY` and `HAVING` are rejected before execution; grouped
  `ProjectionQuery` results are supported, and every non-aggregate projected or
  typed `ORDER BY` column must also appear in `GROUP BY`;
- a `DISTINCT` projection may order only by a typed column present in the
  projection; raw or unprojected ordering is rejected before SQL execution;
- PostgreSQL's native NULL ordering is preserved (`ASC` places NULL last and
  `DESC` places it first), which differs from SQLite; use an intentional
  backend-specific raw order when explicit NULL placement is required;
- PostgreSQL `LIKE` is case-sensitive, while SQLite's default ASCII `LIKE` is
  case-insensitive; the ORM preserves each backend's native behavior;
- UUID, date, time, binary/blob, converter, migration, async, and connection-pool
  APIs are not included;
- model table names are double-quoted identifiers, not schema-qualified paths;
  choose an application schema through PostgreSQL `search_path`;
- table, column, relation, and generated alias identifiers must fit PostgreSQL's
  63-byte identifier limit and are rejected instead of being silently truncated;
- the maximum bind-parameter count is 65,535 and is checked before execution;
- `unsigned long long` is exchanged through PostgreSQL `BIGINT` and is limited
  to `INT64_MAX`; a larger value produces `DatabaseErrorCode::Conversion`.
- C++ `float` columns use `DOUBLE PRECISION` so a value first promoted by SOCI
  can round-trip without decimal-text narrowing; C++ `double` uses the same SQL
  type.
- embedded NUL bytes in bound `TEXT` values are rejected instead of being
  silently truncated by C-string APIs; projected PostgreSQL `SUM` and `AVG`
  values use an exact text transport before conversion to the requested C++
  numeric field.

PostgreSQL aborts an explicit transaction after a statement error. The ORM
tracks that state: `commitTransaction()` returns
`DatabaseErrorCode::Transaction`, and the application must call
`rollbackTransaction()` before continuing.

Live tests require a dedicated database role that may create and drop schemas.
Each test creates a random `orm_cxx_test_...` schema, sets it as `search_path`,
and removes it with a prefix-validated `DROP SCHEMA ... CASCADE`. Tests never
target `public` or a caller-provided schema name. Set the DSN and enable the live
profile explicitly:

```sh
export ORM_CXX_POSTGRESQL_TEST_DSN='postgresql://host=localhost port=5432 dbname=orm_cxx user=orm_cxx password=orm_cxx'
cmake --workflow --preset linux-gcc-postgresql
```

The Compose service provides a disposable PostgreSQL 18 instance:

```sh
docker compose --profile postgresql up --detach --wait postgres
docker compose run --rm \
  -e ORM_CXX_POSTGRESQL_TEST_DSN='postgresql://host=postgres port=5432 dbname=orm_cxx user=orm_cxx password=orm_cxx' \
  dev cmake --workflow --preset linux-gcc-postgresql
```

## Build configuration

SQLite is controlled by `ORM_CXX_ENABLE_SQLITE_BACKEND` and enabled by default.
PostgreSQL is controlled by `ORM_CXX_ENABLE_POSTGRESQL_BACKEND` and disabled by
default. Live tests additionally require
`ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS=ON`. The target name and alias remain
`orm-cxx` and `orm-cxx::orm-cxx`.

For a backend-neutral consumer build that links only the shared SOCI core, use:

```sh
cmake -S . -B build/core-only \
  -DORM_CXX_ENABLE_SQLITE_BACKEND=OFF \
  -DORM_CXX_ENABLE_POSTGRESQL_BACKEND=OFF \
  -DORM_CXX_BUILD_TESTS=OFF \
  -DORM_CXX_BUILD_EXAMPLES=OFF
```

With a backend disabled, its implementation sources, SOCI driver link dependency,
and factory registration are omitted; `soci_core` remains required. The legacy
developer test suite still requires SQLite. Examples are selected according to
the enabled backends. Parent-project consumers default tests and examples to
`OFF` and may choose SQLite-only, PostgreSQL-only, both backends, or core-only.

Linux PostgreSQL builds require the `libpq` development package. The repository
vcpkg manifest enables its `sqlite` feature by default and keeps `postgresql`
optional:

```powershell
.\externals\vcpkg\vcpkg.exe install --x-feature=postgresql
cmake --workflow --preset msvc-postgresql-debug
```

For a PostgreSQL-only dependency set, omit the default SQLite feature:

```powershell
.\externals\vcpkg\vcpkg.exe install --x-no-default-features --x-feature=postgresql
```

The vcpkg features provision native dependencies; the corresponding
`ORM_CXX_ENABLE_*_BACKEND` CMake options still select which adapters are built.

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
