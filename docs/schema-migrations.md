# Schema migrations

`import orm;` exports `orm::migrations`, a versioned SQL executor for SQLite and
PostgreSQL. It is part of the library target and needs no additional dependency,
package or build option. SQL migrations remain independent of current C++ model
types. Apply migrations before obtaining data using the new model mapping.

## Embedded SQL and the API

```cpp
#include <string>
import orm;

orm::migrations::Catalog catalog{
    {10, "create_users", {
        {orm::db::BackendType::Sqlite, {
            "CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT NOT NULL);",
            "DROP TABLE users;"}},
        {orm::db::BackendType::Postgres, {
            "CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT NOT NULL);",
            "DROP TABLE users;"}}
    }},
    {30, "add_email", {
        {orm::db::BackendType::Sqlite, {
            "ALTER TABLE users ADD COLUMN email TEXT;",
            "ALTER TABLE users DROP COLUMN email;"}},
        {orm::db::BackendType::Postgres, {
            "ALTER TABLE users ADD COLUMN email TEXT;",
            "ALTER TABLE users DROP COLUMN email;"}}
    }}
};

orm::Database database;
database.connect("sqlite3://application.db");
orm::migrations::Runner runner{database, catalog};
const auto before = runner.status();
runner.validate();
const auto plan = runner.preview(orm::migrations::Direction::Up);
runner.up();       // latest catalog version
runner.down(10);   // undo version 30
runner.up(30);     // apply through the exact target version
```

`Catalog` owns its SQL and sorts migrations by version. A version is a positive
signed 64-bit integer; gaps are allowed, duplicates are rejected. Names contain
ASCII letters, digits, underscores or hyphens. Each migration has at least one
backend variant, with nonempty `up` SQL and optional `down` SQL. The selected
database backend must have a variant for every catalog entry.

`Runner` owns a catalog copy and borrows a connected, idle `Database` that must
outlive it. Migration commands cannot run inside an application transaction.
They do not require an `orm::Schema` or ORM context.

`status()` returns applied records, pending versions, current version, baseline
version and history diagnostics. `validate()` throws if history disagrees with
the catalog. `preview()` returns an owned plan containing the exact normalized
SQL, versions and execution order. These operations never create a history
table or change application data. They read within a transaction.

`up()` defaults to the latest version. Explicit targets must be a catalog
version or zero and must agree with the direction. `down(0)` undoes all ordinary
migrations. Downgrades run in reverse order; the entire requested plan is checked
for missing `down` scripts before the first change. A supplied empty `down`
script explicitly means that undoing the migration requires no SQL.

## SQL files

Load the same catalog contract with `Catalog::fromDirectory("migrations")`:

```text
migrations/
  10_create_users/
    sqlite/
      up.sql
      down.sql
    postgresql/
      up.sql
      down.sql
  30_add_email/
    sqlite/
      up.sql
    postgresql/
      up.sql
```

`down.sql` is optional; a missing file makes that migration irreversible. Other
files, unexpected dialect directories, symlinks and malformed migration
directories are rejected. Backend variants can differ and do not need to share
the same SQL. The loader reads all SQL into memory before execution. Initial
UTF-8 BOMs are removed and CRLF is converted to LF for both file and embedded
sources. Embedded NUL bytes are rejected. Whitespace beyond that normalization
is significant.

## Project CLI

Compile a small executable registering your own catalog:

```cpp
import orm;

int main(int argc, char** argv)
{
    const auto catalog = orm::migrations::Catalog::fromDirectory("migrations");
    return orm::migrations::runCommandLine(argc, argv, catalog);
}
```

```cmake
add_executable(app-migrate migrate.cpp)
target_link_libraries(app-migrate PRIVATE orm-cxx::orm-cxx)
```

Set `ORM_CXX_DATABASE_URL` in the process environment. It accepts the same
`sqlite3://...` or `postgresql://...` keyword/value connection string as
`Database::connect`. Connection strings are not accepted as CLI arguments and
are not printed in diagnostics. Use your deployment's existing secret handling
to supply credentials.

```sh
app-migrate status
app-migrate validate
app-migrate preview up
app-migrate up --to 30
app-migrate preview down --to 10
app-migrate down --to 10
app-migrate baseline --to 10
```

`--help`, `help`, or no arguments show usage without connecting. Exit codes are
0 for success, 1 for migration/connection/history failures and 2 for invalid
arguments. The stream-based overload accepts arguments excluding the executable
name and allows applications to capture the output. The ready-to-build
[example](../examples/migrations.cpp) uses an embedded catalog and the
`migrations-example` target.

## History and baseline

The executor owns `_orm_migrations` in SQLite's `main` database or PostgreSQL's
current schema. It stores `version`, `name`, `backend`, `checksum`, `applied_at`
and `baseline`. The table is created by the first successful write command.
Application code and migration scripts must not modify this table.

Applied migrations must be a matching prefix of the catalog. Each checksum is
SHA-256 over a versioned, length-prefixed encoding of normalized `up` and `down`
SQL, including an explicit marker for absent `down`. Changing either script,
renaming an applied migration, removing it, inserting an earlier version, or
changing its backend blocks further changes. Append a new migration instead of
editing applied ones. There is no automatic repair or checksum rewriting.

For an existing database, first verify manually that its schema and data match
the proposed catalog prefix, then call `baseline(version)` or the corresponding
CLI command. Baseline requires empty history and a positive catalog version.
It records the complete prefix atomically without executing its SQL. Later
downgrades cannot cross the baseline boundary. This is an explicit assertion
by the operator; baseline does not inspect or infer the application's schema.

## Transactions, concurrency and errors

Each migration and its history update share one transaction. A failure rolls
back the current migration, including its DDL and data writes. Earlier completed
migrations remain committed, allowing a corrected pending migration to be
retried. Baseline records its prefix in one transaction.

SQLite uses `BEGIN IMMEDIATE`. PostgreSQL uses a transaction-scoped advisory
lock derived from the qualified history table. A competing runner fails without
retrying. Before every write transaction, history is read again and compared
with the expected state; a change between steps requires inspecting the new
state and retrying explicitly. The lock is released between migrations.

`MigrationError` exposes `ErrorCode`, the migration version, backend and optional
native error code. Diagnostics omit SQL and connection strings. Catalog/target
errors, changed checksums, missing undo scripts, baseline boundaries, lock
conflicts and SQL failures have distinct codes. An uncertain commit disconnects
the borrowed database and returns `CommitUncertain`. Reconnect and inspect
history before retrying: the server may already have committed the migration.

## SQL contract and limits

Scripts contain backend-native SQL, including multiple statements, comments,
quoted semicolons, SQLite triggers and PostgreSQL dollar-quoted function bodies.
SQLite uses the native prepare/tail parser, and PostgreSQL executes the complete
script through libpq. SQL is not split on semicolons.

The executor owns transaction boundaries. Scripts cannot contain explicit
transaction or savepoint commands, client commands such as `\\i`, SQLite
`ATTACH`/`DETACH`, or changes to connection settings. SQLite permits
`PRAGMA defer_foreign_keys` for transactional table rebuilds, but cannot disable
foreign key enforcement inside a migration. PostgreSQL permits transaction-local
`SET CONSTRAINTS`; general `SET`, `RESET`, `DISCARD`, two-phase transactions,
`COPY ... FROM STDIN` / `TO STDOUT` and unquoted `BEGIN ATOMIC` function bodies
are excluded. PostgreSQL requires `standard_conforming_strings=on` and checks
that scripts preserve the search path, role, session authorization and string
literal setting.
Operations requiring execution outside a transaction, such as PostgreSQL
`CREATE INDEX CONCURRENTLY`, must use a separate deployment procedure.

SQLite changes beyond supported `ALTER TABLE` operations require handwritten
table rebuilds: create a replacement, copy and transform data, drop/rename, and
restore indexes, triggers and foreign keys. Test upgrade and downgrade SQL on
representative data. A `down` script provides an explicit operation; it cannot
recover data discarded by the upgrade.

This release executes supplied SQL. Schema snapshots, model diffs, automatic
rename inference, a migration generator and automatic schema synchronization
are future work. Custom backend providers must explicitly advertise
`schema.transactionalMigrations` and implement the migration runtime hooks;
the built-in implementation supports SQLite and PostgreSQL.
