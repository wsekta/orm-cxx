# ORM C++ Roadmap

This roadmap is focused on making `orm-cxx` easier to adopt as an open source
C++ ORM: keep the SQLite workflow stable, document the public model and query
contracts, then expand expressiveness and database support deliberately.

## Current State

`orm-cxx` provides a usable SQLite-first foundation with an optional PostgreSQL backend:

- C++20 model metadata based on reflected struct fields.
- A documented SQLite model contract for supported scalar fields, nullable
  fields with `std::optional<T>`, table and column name mapping, default and
  explicit primary keys, SQLite auto-increment primary keys, and one-level
  one-to-one relations.
- Nullable one-to-one relations through `std::optional<RelatedModel>`, including
  nullable local foreign-key columns, SQL `NULL` inserts, joined selects, and
  non-joined selects that hydrate related primary-key values.
- Table creation and drop commands.
- Full row-level CRUD operations through `orm::Database`, including insert,
  select, predicate-based update, and predicate-based remove operations.
- A query builder for full-model `SELECT` queries with predicates, ordering,
  `DISTINCT`, `LIMIT`, `OFFSET`, `GROUP BY`, aggregate `HAVING`, raw predicates,
  raw ordering, and bind parameters.
- A projection query builder for partial-result `SELECT` queries that hydrate
  flat DTOs through explicit field aliases.
- Aggregate projection queries with `count`, `countAll`, `sum`, `avg`, `min`,
  `max`, `GROUP BY`, and aggregate `HAVING` predicates.
- Query examples that show full-model selects, full-model grouping, projected
  selects, and aggregate projection queries side by side.
- A patch-style `orm::Update<T>` builder for safe, filtered `UPDATE`
  statements, nullable-field updates through `std::nullopt`, and affected-row
  counts for `UPDATE` and `DELETE`.
- One-level one-to-one relation support in model metadata, inserts, select
  joins, related-field query paths, and write operations targeting local
  foreign-key columns through related primary-key paths.
- Explicit `OneToMany` and owning or inverse `ManyToMany` mappings with
  collection wrappers, lazy metadata target resolution, and validation for
  primary keys, inverse fields, junction names, and composite-key column
  counts.
- SQLite junction-table DDL plus idempotent `link` and `unlink` mutations.
  Collection mutations never cascade-save endpoint models; junction rows use
  foreign keys and `ON DELETE CASCADE` without deleting the opposite endpoint.
- One-level `Query<T>::include` loading with parameter-bounded batched relation
  queries per included field, complete collections under parent pagination,
  and an explicit loaded-versus-unloaded wrapper state.
- Collection predicates through correlated `any`, `exists`, and `none`
  expressions without implicitly loading the matching collection.
- Unit and integration tests across model metadata, command rendering, query
  behavior, write operations, transactions, and SQLite execution.
- Target-scoped CMake configuration with independent library, example, test,
  coverage, and dependency boundaries plus maintained GCC, Clang, coverage,
  MSVC, and quality presets.
- Repository-wide formatting and static-analysis policy through `.clang-tidy`,
  `.cmake-format.yaml`, local quality scripts, and matching GitHub Actions jobs.
- A shared Docker, Compose, and devcontainer environment with documented
  one-command paths for the supported Linux CI checks and an equivalent MSVC
  workflow.
- CI coverage for GCC, Clang, MSVC, Codecov, formatting, and static analysis.
- A source-level backend provider contract that centralizes backend selection,
  capabilities, runtime limits, session hooks, value binding, SQL dialect
  behavior, command generation, affected-row normalization, and driver-error
  translation.
- Stable backend-neutral database errors for lifecycle, capability, connection,
  statement, constraint, transaction, conversion, and affected-row failures.
- Reusable backend conformance configuration and tests, with SQLite exercising
  the shared public behavior on GCC, Clang, and MSVC.
- An optional SQLite build boundary: core-only consumers can omit SQLite
  sources, registration, and driver linkage while retaining the shared ORM and
  SOCI core.
- A documented backend support matrix, portability policy, source extension
  contract, feature negotiation rules, and minimum CI requirements.
- An optional PostgreSQL adapter with native dialect/runtime behavior, isolated
  live conformance tests, PostgreSQL 15 and 18 CI boundaries, and SQLite-only,
  PostgreSQL-only, combined, and core-only consumer builds.

## Long Term

SQLite and PostgreSQL now exercise the backend portability contract. Continue
expanding that contract only where another production backend needs it.

- Add MySQL, ODBC, Oracle, Firebird, and DB2 according to user demand and
  maintainer capacity.
- Package the library for vcpkg and Conan once the public API has settled.
- After adding non-SQLite backends with native date/time and UUID types, decide
  how custom field converters should work before supporting types such as
  `std::tm`, `boost::uuids::uuid`, `boost::optional`, or other third-party
  values. SQLite has no native UUID type, so defining this abstraction before
  those backends exist would not provide a useful portability contract.

## Far Future

These items are valuable, but they should not block the core ORM experience.

- Reduce dependency risk around `reflect-cpp`, or replace it with a dedicated
  reflection layer if that becomes practical.
- Move more metadata and query validation work to compile time.
- Define and document thread-safety guarantees.
- Add structured logging hooks.
- Explore coroutine-based APIs for asynchronous or pipelined database work.

## Roadmap Principles

- Prefer stable, documented behavior over broad but shallow feature coverage.
- Keep SQLite excellent before adding more backends.
- Add public APIs only when their behavior, error cases, and tests are clear.
- Treat documentation and examples as part of the feature, not follow-up polish.
