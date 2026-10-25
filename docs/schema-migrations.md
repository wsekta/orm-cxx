# Schema migrations

`import orm;` exports `orm::migrations` for SQLite, PostgreSQL, MySQL and MariaDB.
A project compares the application's current `Schema::view` with its last ready
physical schema snapshot and generates backend SQL without connecting to a database.
Generation supports all four backends even in a core-only library build. Native
client libraries are needed only for the runtime adapters used to execute SQL.

## Application CLI

Register the application's schema in a small executable:

```cpp
#include <optional>
#include <string>
import orm;

struct User {
    int id;
    std::string name;
    std::optional<std::string> email;
    inline static constexpr orm::reflection::FixedString table_name{"users"};
};
using AppSchema = orm::Schema<User>;

int main(int argc, char** argv) {
    return orm::migrations::runCommandLine(argc, argv, AppSchema::view);
}
```

```cmake
add_executable(app-migrate migrate.cpp)
target_link_libraries(app-migrate PRIVATE orm-cxx::orm-cxx)
```

The [example](../examples/migrations.cpp) builds as `migrations-example`.
Generation commands use the `migrations` directory by default; the API accepts
an explicit project directory. They never read connection environment fields.

```sh
app-migrate init
# Or select a fixed backend subset when initializing:
app-migrate init --backends sqlite,postgresql
app-migrate compare
app-migrate compare --check
app-migrate make initial
# Review each generated SQL script and its declared target schema:
app-migrate finalize 1 --accept-schema
```

`compare --check` returns 1 when the models differ, making it suitable for a CI
check that model changes have a migration. `make` creates a draft. A draft cannot
be executed or used as the source of another migration. Finalization requires
that the current models match its target snapshot and explicitly accepts that
the reviewed SQL produces the declared schema. It validates the script contract;
it does not infer the result of arbitrary SQL.

Once finalized, a migration is immutable. The backend set selected by `init` is
fixed for the project. Create another migration for subsequent model changes.

## Durable state and connection loss

The migration state protocol owns the lock, history and durable marker rules
shared by migration execution, baseline acceptance and operator resolution.
The runner keeps physical-schema checks and SQLite rebuild session restoration
separate from those state transitions.

SQLite and PostgreSQL commit a migration's SQL and history together. MySQL and
MariaDB commit a durable marker before executing nontransactional DDL, retain
the session lock across commits, and clear the marker together with the final
history update. A lost connection can therefore leave schema changes with an
incomplete marker. Inspect the database and use explicit resolution; the runner
does not retry the SQL automatically.

After an uncertain commit, reconnect and inspect `status()` before deciding
what to do. A committed baseline remains a complete baseline. A committed
resolution remains recorded even when its response was lost. A failed history
transaction retains the previous history and any durable incomplete marker.
`CommitUncertain` remains distinct from `SessionRestoration`, which reports a
committed migration whose SQLite session settings could not be restored.

The failure tests run actual scripts and commits on the configured databases,
lose the connection before or after those operations, and inspect the same
database through a new connection. These tests cover execution in both
directions, baseline and both resolution choices. They do not simulate server
power loss. SQLite uses a file for these reconnection tests.

## Project files

```text
migrations/
  project.json
  1_initial/
    migration.json
    schema.json
    ready.json                 # Created atomically by finalize
    sqlite/up.sql
    postgresql/up.sql
    mysql/up.sql
    mariadb/up.sql
  2_email/
    migration.json
    schema.json
    ready.json
    sqlite/up.sql
    sqlite/down.sql            # Optional, independently for each backend
    postgresql/up.sql
    mysql/up.sql
    mariadb/up.sql
```

`project.json` declares the format and backend set. `schema.json` owns physical
names, frozen native SQL types, nullability, string lengths, identity properties,
ordered primary and foreign keys, managed indexes and table options. It stores
no C++ type names, addresses or reflection handles. Model declaration order does
not produce spurious migrations; primary-key column order remains significant.
`migration.json` binds the parent version and snapshot, direction-specific
execution contexts and selected-backend before/after hashes.

A writer lock prevents concurrent project mutations. `make` writes a staging
directory and publishes the complete draft by renaming it. `finalize` atomically
publishes `ready.json` last. The ready manifest verifies SQL, snapshots, metadata
and execution checksums on every load. Unknown entries, unsupported formats,
missing files and symlinks are rejected. An interrupted staging directory requires
inspection before another writer can proceed.

`Catalog::fromDirectory("migrations")` loads this project format and rejects
unfinalized drafts. It reads all files before opening a database connection.
Backend variants are independent: MariaDB never falls back to MySQL SQL.
UTF-8 BOMs and CRLF are normalized in SQL; embedded NUL bytes are rejected.
Other SQL whitespace is significant. JSON is a private implementation dependency
and is absent from the public API.

## Model changes and decisions

The generator handles tables, columns, explicit renames, types, string lengths,
nullability, identity, ordered primary keys, foreign keys and owning junction tables.
New tables, nullable columns and proven lossless widening can be generated directly.
Potentially destructive changes require approval for each reported change ID.
Renames must be declared explicitly; names are never matched heuristically.

Use `compare` to obtain exact change IDs, then supply a decisions JSON file:

```json
{
  "tableRenames": [{"from": "users", "to": "people"}],
  "columnRenames": [{"table": "people", "from": "name", "to": "display_name"}],
  "allowDataLossUp": [],
  "allowDataLossDown": [],
  "backfills": [{"table": "people", "column": "active", "constant": true}],
  "conversions": [],
  "downBackfills": [],
  "downConversions": []
}
```

```sh
app-migrate compare --decisions decisions.json
app-migrate make rename_users --decisions decisions.json
```

A transformation has exactly one value source: a typed JSON scalar `constant`,
a `sourceColumn`, or `expressions` mapping backend names to SQL expressions.
For example, `"expressions":{"sqlite":"CAST(old_value AS TEXT)",
"postgresql":"old_value::text","mysql":"CAST(old_value AS CHAR)",
"mariadb":"CAST(old_value AS CHAR)"}`. Adding a required column or tightening
nullability requires a backfill. Conversions are explicit when losslessness
cannot be established. Decisions for upgrades and downgrades are separate.

Native temporal and UUID constants use a tagged object. Existing boolean,
numeric, string and `null` constants retain their meaning:

```json
{
  "backfills": [
    {"table": "events", "column": "date", "constant": {"type": "date", "value": "2026-10-24"}},
    {"table": "events", "column": "starts_at", "constant": {"type": "time_of_day", "value": "09:00:00.123456"}},
    {"table": "events", "column": "starts_local", "constant": {"type": "local_date_time", "value": "2026-10-24 09:00:00.123456"}},
    {"table": "events", "column": "recorded_at", "constant": {"type": "utc_timestamp", "value": "2026-10-24 07:00:00.123456+00:00"}},
    {"table": "events", "column": "external_id", "constant": {"type": "uuid", "value": "019a1122-3344-7788-99aa-bbccddeeff00"}}
  ]
}
```

The C++ `GenerationLiteral` variant also accepts `Date`, `TimeOfDay`,
`LocalDateTime`, `UtcTimestamp` and `Uuid` directly. The generator validates the
logical target type, calendar/range and native backend support before rendering
a backend literal. UUID constants require PostgreSQL or MariaDB; SQLite native
values and MySQL UUID are rejected. See [Value types](value-types.md).

Temporal snapshots preserve `(6)` precision and with/without-time-zone semantics.
Native drift inspection compares those properties explicitly; changing `(6)`
to `(0)` is drift. Changing `LocalDateTime` to `UtcTimestamp` requires an explicit
conversion that chooses the interpretation of existing civil times. It is not
an automatic lossless type change. External columns with smaller precision must
be brought into the declared schema contract; queries do not introspect the
whole database before each operation.

The transformation's `table` and `column` identify the target schema for that
direction, after any declared renames. `sourceColumn` identifies a column in the
original source table. SQL expressions also use the original table and column
names. All generated staging values are prepared before any source table is
renamed or dropped, so expressions may read other source tables that the migration
will subsequently remove.

Safe inverse operations can produce `down.sql` automatically. An inverse that
may discard data needs its own approval. A missing `down.sql` means that the
migration cannot be undone. Dropped information cannot be recovered by recreating
its column; supply an explicit inverse transformation when a meaningful inverse
exists. Review and test both directions with representative data.

For data migrations or SQL outside the managed model, use:

```sh
app-migrate make populate_reference_data --manual
# Edit the backend up.sql files; optionally add down.sql.
app-migrate finalize 3 --accept-schema
```

Manual migrations use the same snapshots, review step and integrity checks.
Finalization is an operator assertion about the declared schema, so custom objects
must also be maintained explicitly.

## C++ API and execution

```cpp
const std::array backends{orm::db::BackendType::Sqlite};
auto project = orm::migrations::MigrationProject::init("migrations", backends);
const auto difference = project.compare(AppSchema::view);
const auto version = project.make("initial", AppSchema::view);
// Inspect the generated files.
project.finalize(version, AppSchema::view, true);

orm::Database database{orm::SqliteConfig{.database = std::filesystem::path{"application.db"}}};

orm::migrations::Runner runner{database, project.catalog()};
runner.validate();
const auto plan = runner.preview(orm::migrations::Direction::Up);
runner.up();
```

`capture`, `compare` and `generate` also work directly on owned snapshot values.
Embedded `Catalog` values can contain generated migrations or explicit manual
`Migration`/`SqlMigration` values. Embedded manual scripts with empty snapshots
use the normal execution context. If snapshots are supplied, their backend sets,
hashes and execution contexts are validated together.

`Runner` borrows an idle connected `Database` that must outlive it. Apply migrations
before accessing data with new model mappings. `status`, `validate` and `preview`
read history without creating tables or changing application data. `up()` selects
the latest version; an explicit target must be an exact catalog version or zero.
`down(target)` checks the whole requested downgrade for missing scripts before
executing its first step. A present empty down script declares a no-op inverse.

Execution commands require `ORM_CXX_DATABASE_BACKEND` (`sqlite`, `postgresql`,
`mysql`, `mariadb`). Supply connection fields through `ORM_CXX_DATABASE_NAME`,
`HOST`, `PORT`, `USER`, `PASSWORD` with the same `ORM_CXX_DATABASE_` prefix.
The CLI constructs typed configurations; the library does not read environment
variables. A missing field stays omitted, while an explicitly empty string stays
empty. For SQLite, NAME is required and `:memory:` explicitly selects memory.

Backend-specific suffixes are `SOCKET`, `CONNECT_TIMEOUT_SECONDS`,
`BUSY_TIMEOUT_MS`, `PASSFILE`, `SSL_MODE`, `SSL_CA`, `SSL_CERT`, `SSL_KEY`, and
`SQLITE_OPEN_MODE`. Fields inappropriate for the selected backend are rejected.
Ports and timeouts require complete decimal values within their native ranges.
PostgreSQL SSL modes are `disable|allow|prefer|require|verify-ca|verify-full`;
MySQL-family modes are `disabled|preferred|required|verify-ca|verify-identity`.
SQLite modes are `read-write-create|read-write|read-only`. Passwords belong in
the deployment environment and are omitted from diagnostics and process arguments.

```sh
app-migrate status
app-migrate validate
app-migrate preview up
app-migrate up --to 2
app-migrate preview down --to 1
app-migrate down --to 1
app-migrate baseline --to 1
app-migrate resolve --version 2 --checksum <64-lowercase-hex-digits> --resolution completed
app-migrate resolve --version 2 --checksum <64-lowercase-hex-digits> --resolution reverted
```

Help needs no project or connection. Exit codes are 0 for success, 1 for model,
project, database or history failures, and 2 for invalid arguments. The stream-based
CLI overload accepts arguments excluding the executable name.

## History, transactions and recovery

The executor owns `_orm_migrations` in SQLite's `main` database, PostgreSQL's
current schema, or the selected MySQL/MariaDB database. It stores version, name,
backend, checksum, application time and baseline status. Applied migrations must
be a matching catalog prefix. Checksums bind normalized up/down SQL, explicit
absence of down, both execution contexts and the selected backend's frozen
before/after snapshots. Changes to an applied version block further execution.

`baseline(version)` records a verified existing schema's catalog prefix without
executing its SQL. This requires empty history and a positive version. It is an
explicit operator assertion; downgrades cannot cross the baseline boundary.

SQLite and PostgreSQL commit each migration and its history update atomically.
SQLite uses `BEGIN IMMEDIATE`; PostgreSQL uses a transaction-scoped advisory lock.
History is rechecked under the lock before every write. Earlier completed
migrations remain committed if a later migration fails.

Generated SQLite rebuilds use a checksummed execution context. The runner preserves
the session's foreign-key setting, disables enforcement before beginning the
transaction, validates source tables and unknown objects, and restores the setting
after completion or rollback. It creates and copies all staging tables before
dropping originals, preserving cyclic and self-referencing relationships. Preserved
identity columns retain their `sqlite_sequence` high-water marks, including values
above the greatest surviving row. A full `foreign_key_check` runs before committing
history. Unknown triggers/views and unmanaged affected-table indexes block a
rebuild rather than being silently discarded. Schema drift blocks destructive work.

Native backend preflight also checks table and column options, ordered keys and
the definitions of managed indexes. Replacing a managed index with a descending,
partial, expression, prefix or otherwise unsupported variant is schema drift.
Unmanaged incoming foreign keys also block changes to their referenced tables.

MySQL and MariaDB implicitly commit DDL and expose `NonTransactional` atomicity.
A session `GET_LOCK` covers execution. Before running SQL, the runner durably records
an incomplete marker in `_orm_migrations_incomplete`. Successful history updates
and marker deletion share an InnoDB transaction. A failure leaves the marker and
may leave successful DDL statements; the runner never replays them automatically.

An incomplete marker blocks mutation, validation and preview. Inspect the schema
and data, complete the script or restore its preceding state, then use the original
catalog and exact checksum with `resolve(..., Completed)` or `resolve(..., Reverted)`.
Both outcomes are explicit operator assertions and remove the marker under the lock.

`MigrationError` exposes a code, version, backend and optional native error code.
An uncertain commit disconnects the database and reports `CommitUncertain`.
Reconnect and inspect history before retrying. A confirmed commit followed by a
failed session-setting restoration is reported separately as `SessionRestoration`;
the committed migration must not be treated as rolled back.

## SQL contract

Scripts contain backend-native SQL with multiple statements and quoted bodies.
SQLite uses prepare/tail parsing; PostgreSQL sends the whole script through libpq;
MySQL/MariaDB execute native multi-statements and drain all results. SQL is never
split on semicolons.

Temporal session invariants are checked before scripts and protected through
execution: PostgreSQL uses ISO/YMD and UTC; MySQL/MariaDB use UTC, strict mode
and explicit timestamp defaults. Reconnect restores those settings. Scripts
cannot change them to reinterpret timestamp values or alter generated types.
Native date/time support does not change the migration history format,
`applied_at`, durable markers or the migration state protocol.

The executor owns transaction boundaries. Scripts cannot control transactions,
savepoints, migration ledgers or locks, or change session settings. SQLite scripts
cannot disable foreign keys; that action belongs to the explicit rebuild context.
PostgreSQL permits transaction-local `SET CONSTRAINTS`, but rejects general session
changes, two-phase transactions and client streaming commands. SQL requiring a
separate transaction, such as `CREATE INDEX CONCURRENTLY`, needs a separate deployment
procedure. MySQL/MariaDB executable comments are checked as SQL; dynamic prepared
SQL and client `DELIMITER` commands are excluded. Trigger `SET NEW.column` and
`UPDATE ... SET` remain supported.

Custom providers explicitly advertise migration support and atomicity and implement
history, lock and native script hooks. Nontransactional providers additionally need
a durable journal and session-lock release. Runtime support is independent of the
core offline generator.
