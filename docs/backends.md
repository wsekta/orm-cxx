# Backends

`orm-cxx` treats SQLite, PostgreSQL, MySQL and MariaDB as equal, independently
selected adapters. All four build options default to `OFF`. The default library
and developer test profile use the backend-independent core and SOCI core.

## Support status

| Backend | Status | Verification |
| --- | --- | --- |
| SQLite | Supported | Integration tests on GCC, Clang, and MSVC; Clang coverage |
| PostgreSQL | Supported | Live common conformance on PostgreSQL 15 and 18; GCC and Clang; Clang coverage; MSVC compile/link |
| MySQL | Supported | Common conformance for 8.4 and 9.7; GCC and Clang; Clang coverage; MSVC compile/link |
| MariaDB | Supported | Common conformance for 11.4, 11.8 and 12.3; GCC and Clang; Clang coverage; MSVC compile/link |
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

SQLite, PostgreSQL, MySQL and MariaDB are equal supported backends for the shared ORM
contract. Supported does not mean that their native SQL semantics or every
optional capability are identical. Applications can use the same model, CRUD,
projection, transaction, and relation APIs; capability reporting exposes the
intentional differences before SQL execution.

| Contract area | SQLite | PostgreSQL | MySQL | MariaDB |
| --- | --- | --- | --- | --- |
| Build mode | Explicit opt-in | Explicit opt-in | Explicit opt-in | Explicit opt-in |
| Scalar and nullable fields | Portable contract | Portable contract | Portable contract | Portable contract |
| Manual, composite and generated integer keys | Supported | Supported | Supported | Supported |
| CRUD and exact affected rows | Supported | Supported | Supported | Supported |
| Projections and aggregate projections | Supported | Supported | Supported | Supported |
| Full-model grouping | Representative-row semantics | Rejected | Rejected | Rejected |
| Grouped projections | Supported | Strict grouping | Strict grouping | Strict grouping |
| To-one and collection relations | Supported | Supported | Supported | Supported |
| Foreign keys and junction cascade | Enabled per ORM session | Native enforcement | InnoDB enforcement | InnoDB enforcement |
| Explicit transactions | Supported | Statement errors require rollback | Fatal transaction errors require rollback | Fatal transaction errors require rollback |
| [Versioned migrations](schema-migrations.md) | Transactional; `BEGIN IMMEDIATE` | Transactional; advisory lock | Nontransactional; session lock and journal | Nontransactional; session lock and journal |
| String primary/foreign keys | Native text | Native text | Explicit bounded strings | Explicit bounded strings |
| ORM parameter ceiling | 900 | 65,535 | Runtime/driver limits | Runtime/driver limits |
| Identifier ceiling | No additional byte limit | 63 bytes | 64 characters | 64 characters |
| ORM `LIKE` behavior | ASCII case-insensitive | Case-sensitive | Binary collation | Binary collation |
| Native NULL order | ASC first; DESC last | ASC last; DESC first | ASC first; DESC last | ASC first; DESC last |
| Namespace selection | Database connection | Active `search_path` | Selected database | Selected database |

Raw SQL remains backend-specific. Code that needs portable behavior should use
the typed API and inspect `getBackendCapabilities()` for optional operations.
For every bundled adapter, a physical non-identity column name used as a SOCI
bind name may contain only ASCII letters, digits, and `_`.
Connection strings and identifiers containing an embedded NUL byte are rejected
before they reach a database driver's C API.

## SQLite

Open with a typed configuration; a filesystem path is converted to UTF-8:

```cpp
orm::Database database{orm::SqliteConfig{.database = std::filesystem::path{"application.db"}}};

```

The configuration type selects the backend. Inspect its negotiated capability profile:

```cpp
orm::Database database{orm::SqliteConfig{.database = orm::SqliteMemory{}}};


const auto& capabilities = database.getBackendCapabilities();
if (capabilities.query.groupBy)
{
    // The typed GROUP BY API is available for this connection.
}
```

Failures that cross the backend boundary use `orm::DatabaseError`. Its code,
backend type, logical operation, and optional native code are stable structured
fields; diagnostic messages never include connection strings or bound values.

SQLite sessions enable foreign-key enforcement for ORM-managed connections.
The same public model, query, mutation, transaction and relation contract applies
to every adapter.

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

Enable the adapter and provide owned, optional connection fields:

```sh
cmake -S . -B build/postgresql -DORM_CXX_ENABLE_POSTGRESQL_BACKEND=ON
```

```cpp
orm::Database database{orm::PostgresConfig{
    .host = "localhost", .port = 5432, .database = "application",
    .user = "application", .password = password,
    .connectTimeout = std::chrono::seconds{5},
    .tls = {.mode = orm::PostgresSslMode::VerifyFull,
            .caCertificate = std::filesystem::path{"ca.pem"}},
}};
```

Omitted fields preserve libpq defaults, including environment and passfile
behavior. `PostgresConfig{}` delegates all connection parameters to libpq.
An explicitly empty string remains distinct from an omitted field, but does not
override native authentication semantics: an empty password may still allow
passfile lookup. `passfile` provides a specific file path. Passwords with spaces,
quotes, backslashes, equals signs and Unicode are transported as data. NUL is
rejected. TLS modes are `Disable`, `Allow`, `Prefer`, `Require`, `VerifyCa`,
`VerifyFull`; optional certificate/key paths do not implicitly select a mode.
No URI parser or authentication retry is involved.

With examples enabled, `backend-connection-example <backend> <database-or-file>`
reports capabilities and disconnects. Server credentials use native client defaults.

The server test matrix covers PostgreSQL 15 and 18. MSVC compiles and links the
adapter and consumer configurations; live server behavior runs on Linux.

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
- UUID, date, time, binary/blob, converter, async, and connection-pool
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
target `public` or a caller-provided schema name. Set typed fields and enable the live
profile explicitly:

```sh
export ORM_CXX_POSTGRESQL_TEST_HOST=localhost
export ORM_CXX_POSTGRESQL_TEST_PORT=5432
export ORM_CXX_POSTGRESQL_TEST_DATABASE=orm_cxx
export ORM_CXX_POSTGRESQL_TEST_USER=orm_cxx
export ORM_CXX_POSTGRESQL_TEST_PASSWORD=orm_cxx
cmake --workflow --preset linux-gcc-postgresql
```

The Compose service provides a disposable PostgreSQL 18 instance:

```sh
docker compose --profile postgresql up --detach --wait postgres
docker compose run --rm \
  -e ORM_CXX_POSTGRESQL_TEST_HOST=postgres -e ORM_CXX_POSTGRESQL_TEST_PORT=5432 \
  -e ORM_CXX_POSTGRESQL_TEST_DATABASE=orm_cxx -e ORM_CXX_POSTGRESQL_TEST_USER=orm_cxx \
  -e ORM_CXX_POSTGRESQL_TEST_PASSWORD=orm_cxx \
  dev cmake --workflow --preset linux-gcc-postgresql
```

## MySQL and MariaDB

MySQL and MariaDB have separate providers, registry entries, capabilities,
error identities and migration SQL variants. They share one SOCI `mysql`
transport and one Oracle MySQL C client. Enabling MariaDB alone does not
register MySQL. Enabling MySQL alone does not register MariaDB. A connection to
the wrong server family is rejected during session initialization.

```cpp
orm::Database mysql{orm::MysqlConfig{.host = "localhost", .database = "application"}};
orm::Database maria{orm::MariadbConfig{.host = "localhost", .port = 3307,
                                      .database = "application"}};
```

ORM sessions use InnoDB,
`utf8mb4`, a binary no-pad collation, strict SQL mode including
`ONLY_FULL_GROUP_BY`, foreign-key checks and explicit autocommit. Automatic
reconnection is rejected because it would discard transaction state and locks.

| Contract area | MySQL | MariaDB |
| --- | --- | --- |
| Scalar and nullable fields, CRUD, projections, generated/composite keys | Supported | Supported |
| To-one, one-to-many and many-to-many relations | Supported | Supported |
| Grouped projections | Strict grouping | Strict grouping |
| Full-model grouping | Rejected | Rejected |
| Foreign keys | InnoDB enforcement | InnoDB enforcement |
| Migration DDL | Nontransactional | Nontransactional |
| String primary/foreign key | Explicit bounded string | Explicit bounded string |
| Identifier limit | 64 characters | 64 characters |
| Unsigned 64-bit integer storage | `BIGINT UNSIGNED` | `BIGINT UNSIGNED` |
| Typed string ordering | Supported | Requires explicit string length up to 16,383 characters |

Ordinary unbounded strings use `LONGTEXT`. Bounded strings use `VARCHAR`;
string primary and foreign keys require explicit `stringLengths` metadata.
The ORM validates index width against the active InnoDB page size before DDL.
See [Model](model.md) for the metadata declaration. The configured binary
collation gives case-sensitive `LIKE` and preserves trailing spaces.

MariaDB sorts `LONGTEXT` using a prefix controlled by `max_sort_length`.
The adapter configures a 65,536-byte sort prefix and rejects typed string
ordering unless `stringLengths` declares at most 16,383 characters. This
prevents silently incorrect ordering of long strings. Storage and filtering
of unbounded strings remain supported. Raw SQL ordering keeps native server
semantics and is the caller's responsibility. The restriction is exposed as
`query.maxOrderedStringLength` in backend capabilities.

Both adapters implement [versioned SQL migrations](schema-migrations.md),
including session locks, checksums and explicit recovery of failed migrations.
MySQL-family DDL cannot be rolled back; read the migration recovery contract
before applying schema changes.

The selected Oracle client supports MySQL's default `caching_sha2_password` and
MariaDB's default `mysql_native_password`. `MysqlConfig` and `MariadbConfig` expose the same optional host, port,
database, user, password, socket, and connection timeout fields, and `MysqlTls`.
TLS modes are `Disabled`, `Preferred`, `Required`, `VerifyCa`, `VerifyIdentity`;
set `tls.caCertificate` and configure the server certificate for verified TLS.
Omitted TLS fields retain native client policy. Unix sockets use `socket`.
MySQL-family `connectTimeout` accepts 1..UINT_MAX seconds; explicit zero is rejected
because SOCI does not represent it. Omission preserves the native timeout.
Missing user/password fields preserve native defaults. Anonymous account selection
is a server rule, not an ORM mode; no fallback follows authentication failure. Optional MariaDB authentication plugins
require matching client plugins and are outside the default package contract.
The package uses `libmysql` in vcpkg and `libmysqlclient` through Conan;
MariaDB Connector/C is not a second dependency. On Debian,
`default-libmysqlclient-dev` selects MariaDB Connector/C. Use
`scripts/install_mysql_client_debian.sh` for the Oracle 8.4 LTS client.

## Build configuration

The independent options are `ORM_CXX_ENABLE_SQLITE_BACKEND`,
`ORM_CXX_ENABLE_POSTGRESQL_BACKEND`, `ORM_CXX_ENABLE_MYSQL_BACKEND` and
`ORM_CXX_ENABLE_MARIADB_BACKEND`. All default to `OFF`; all 16 subsets,
including the core-only developer build, are supported.

```sh
cmake -S . -B build/mysql -G Ninja -DORM_CXX_ENABLE_MYSQL_BACKEND=ON
```

A disabled adapter omits implementation sources, its client and SOCI driver
link, complete generated provider declarations and registry factory. SOCI core
remains required. The target is always `orm-cxx::orm-cxx`.
The generated module exposes four booleans in `orm::config`:
`sqliteBackendEnabled`, `postgresqlBackendEnabled`, `mysqlBackendEnabled` and
`mariadbBackendEnabled`. Parent-project tests and examples default to `OFF`.

Each server has an independent `ORM_CXX_ENABLE_<BACKEND>_INTEGRATION_TESTS`
option and typed `ORM_CXX_<BACKEND>_TEST_*` fields. Live tests use isolated PostgreSQL
schemas or MySQL/MariaDB databases. The test role must be permitted to create
and drop these namespaces. The suffixes are `HOST`, `PORT`, `DATABASE`, `USER`, `PASSWORD`,
`CONNECT_TIMEOUT_SECONDS`, `SSL_MODE`, `SSL_CA`, `SSL_CERT`, `SSL_KEY`, plus
PostgreSQL `PASSFILE` and MySQL-family `SOCKET`. Omitted fields preserve native
client defaults; an explicitly empty string is preserved. SSL mode text follows
the [migration CLI](schema-migrations.md). A failed live connection is an error.

```sh
docker compose --profile all-backends up --detach --wait postgres mysql mariadb
docker compose run --rm \
  -e ORM_CXX_POSTGRESQL_TEST_HOST=postgres -e ORM_CXX_POSTGRESQL_TEST_PORT=5432 \
  -e ORM_CXX_POSTGRESQL_TEST_DATABASE=orm_cxx -e ORM_CXX_POSTGRESQL_TEST_USER=orm_cxx \
  -e ORM_CXX_POSTGRESQL_TEST_PASSWORD=orm_cxx \
  -e ORM_CXX_MYSQL_TEST_HOST=mysql -e ORM_CXX_MYSQL_TEST_PORT=3306 \
  -e ORM_CXX_MYSQL_TEST_DATABASE=orm_cxx -e ORM_CXX_MYSQL_TEST_USER=root \
  -e ORM_CXX_MYSQL_TEST_PASSWORD=orm_cxx \
  -e ORM_CXX_MARIADB_TEST_HOST=mariadb -e ORM_CXX_MARIADB_TEST_PORT=3306 \
  -e ORM_CXX_MARIADB_TEST_DATABASE=orm_cxx -e ORM_CXX_MARIADB_TEST_USER=root \
  -e ORM_CXX_MARIADB_TEST_PASSWORD=orm_cxx \
  dev cmake --workflow --preset linux-clang-all-backends
```

Single-adapter presets have suffixes `sqlite`, `postgresql`, `mysql` and
`mariadb`; `all-backends` enables all four. Base `*-debug` presets select core.
Clang 18 exports independent core and four adapter coverage reports, merged
into the common 100% line coverage gate.

Windows selects dependencies explicitly:

```powershell
.\externals\vcpkg\vcpkg.exe install --x-feature=sqlite --x-feature=postgresql --x-feature=mysql --x-feature=mariadb
cmake --workflow --preset msvc-all-backends-debug
```

The vcpkg features provision dependencies; CMake options select library behavior.
Neither enables a backend implicitly. See [Packaging](packaging.md).

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
fragment written for SQLite may not work with a different PostgreSQL or MySQL
backend. Explicit raw-fragment values remain prepared bind parameters.

Use the typed query API for portable application logic and isolate intentional
raw SQL behind backend-specific application code.

See [Backend portability](backend-portability.md) for the shared behavior and CI
policy, and [Backend extension contract](backend-extension.md) for contributor
requirements.
