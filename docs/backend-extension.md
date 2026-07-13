# Backend extension contract

This document defines how contributors add database backends to `orm-cxx`.
It is a source-level extension contract for implementations built with the
library. It is not a stable binary plugin ABI, shared-library discovery protocol,
or promise that independently compiled backend modules remain compatible across
releases.

Backends are registered as source-level `BackendProvider` implementations.
There is deliberately no runtime discovery or binary plugin ABI. New backends
remain in-tree source contributions until the project defines a separately
versioned plugin interface.

## Concrete extension points

The backend boundary is split into focused contracts:

- `BackendProvider` owns backend identity, connection-string recognition,
  capabilities, and access to the remaining strategies;
- `BackendRuntime` owns session initialization, catalog access, runtime limits,
  canonical value binding, affected-row normalization, and driver-error
  translation;
- `SqlDialect` owns identifier quoting, bind markers, pagination, generated-key
  syntax, and relation-specific SQL forms;
- `CommandGenerator` owns model and query command construction for the selected
  backend;
- `CommandGeneratorFactory` is the registry and deterministic selection point.

`Database` consumes these interfaces and does not branch on individual backend
types. A provider is registered once by `BackendType`; automatic selection must
recognize exactly one provider. Overlapping connection-string recognizers are
treated as ambiguous instead of depending on registry iteration order.

Applications embedding an additional source-level provider pass an owned
registry into `Database`:

```cpp
orm::db::CommandGeneratorFactory backends;
backends.registerBackend(std::make_unique<MyBackend>());
orm::Database database{std::move(backends)};
```

The registry and its providers become owned by the database object. In-tree
backends may also be registered by the factory's default construction path.

## Required backend responsibilities

A backend implementation owns the following responsibilities behind one
centrally selected adapter or equivalent contract:

1. **Identity and selection**
   - provide a stable backend name and type;
   - declare accepted connection-string schemes;
   - reject unknown or disabled backends before generating SQL.
2. **Session lifecycle**
   - open and close the appropriate SOCI session;
   - apply required per-session configuration;
   - support begin, commit, and rollback or reject the backend as incomplete.
3. **Command generation**
   - render create/drop table, insert, select, update, and remove operations;
   - render joins, predicates, ordering, grouping, projection, and pagination;
   - render collection-relation DDL, loading, and mutations when that capability
     is advertised.
4. **Types and values**
   - map supported model column types to database types;
   - bind and hydrate the ORM's canonical C++ value representations;
   - preserve nullable values and detect unsupported or lossy conversions.
5. **Execution semantics**
   - report exact affected rows for public APIs returning a count;
   - provide catalog or introspection operations required by ORM behavior;
   - honor the declared maximum bind-parameter count when batching statements.
6. **Errors**
   - translate driver failures into stable ORM error categories;
   - retain useful native error information without exposing credentials;
   - reject unsupported capabilities before sending SQL.

Transaction support and exact mutation counts are part of the current public API
and therefore required for a supported backend. A backend that cannot provide
them may be developed experimentally but must not be listed as supported.

## Capabilities versus dialect

Do not use capabilities to expose every syntax difference to applications.

A capability is user-visible behavior that may be unavailable or have semantics
that cannot be emulated reliably, for example:

- database-generated integer primary keys;
- full-model grouping;
- enforced foreign keys;
- collection relations;
- native UUID, date, or time values.

A limit is backend data, such as maximum bind parameters per statement or the
largest numeric value a driver can exchange without loss.

Adapter implementation details include, for example:

- identifier quoting and qualification;
- placeholder, pagination, and generated-column syntax;
- `IF EXISTS` or `IF NOT EXISTS` forms;
- conflict handling used for idempotent relation links;
- runtime catalog access used to determine whether a table exists.

Dialect and runtime differences must remain behind the adapter. Shared ORM code
should ask for behavior or a declared limit, not branch on individual backend
enum values.

## SOCI boundary

SOCI is the transport and driver abstraction used by the project. A backend may
use SOCI to open sessions, prepare statements, exchange values, and obtain native
driver information. SOCI does not define the ORM's model contract, command
semantics, capabilities, batching policy, or error categories.

An adapter must verify the actual SOCI backend behavior through live tests.
Merely enabling a SOCI build target is insufficient. Backend-specific SOCI
objects should not be exposed through public model or query APIs.

## Raw SQL

Raw predicates and ordering fragments are passed through as backend-specific SQL.
The adapter is not required to translate them between dialects. Their explicit
values must continue to use prepared bind parameters, and documentation must not
describe a raw fragment as portable unless the relevant dialects are verified.

## Error contract

Backend work must preserve the `DatabaseErrorCode` categories:

- unsupported or unavailable backend;
- unsupported backend feature;
- connection failure;
- statement execution failure;
- constraint violation;
- invalid transaction state;
- conversion or hydration failure;
- unavailable affected-row count.

Errors should identify the backend and logical operation. Native codes may be
attached, and the driver exception may be retained as a nested cause. Tests must
assert categories and state changes rather than vendor-specific English text.
Connection strings, passwords, and tokens must not be included in diagnostics.

## Conformance tests

Every backend must execute the same conformance test sources. Those tests use
only public ORM APIs and cover:

- lifecycle and backend selection;
- schema creation and removal;
- scalar, nullable, mapped-name, manual-key, and composite-key round trips;
- CRUD, binding safety, affected rows, transactions, and pagination;
- full-model and projection query behavior advertised by the backend;
- to-one and collection relation behavior advertised by the backend;
- stable errors for unsupported optional capabilities.

The backend test harness supplies a connection, declared capabilities and limits,
an isolated schema or database, and reliable cleanup. Server credentials come
from the test environment and must never be committed. Tests must not rely on
implicit row order or assume that generated integer keys start at one.

Exact SQL snapshots, catalog queries, session pragmas, and driver instrumentation
belong in backend-specific integration or dialect tests.

## Build and CI integration

A backend contribution must provide:

- its implementation sources and dependency target;
- an explicit build option or feature controlling the driver dependency;
- registration with backend selection;
- a test harness using the common conformance sources;
- backend-specific dialect and session integration tests;
- a real database service in CI for server backends;
- updates to [Backends](backends.md), public documentation, and examples where
  relevant.

The minimum new-backend CI job uses Ubuntu, one supported compiler, the real
database service, and both dialect and conformance tests. Additional compiler,
operating-system, and server-version jobs are required only for combinations the
documentation claims to support. The existing SQLite GCC, Clang coverage, and
MSVC jobs remain the core portability guard.

## Backend Definition of Done

A backend can move from experimental to supported only when:

- no public operation selects it through a scattered backend-specific enum
  branch;
- all required operations and advertised capabilities are implemented;
- unsupported optional behavior fails before SQL execution with the documented
  category;
- the common conformance suite passes against a real database in CI;
- backend-specific type mapping, session setup, catalog access, and dialect SQL
  have focused tests;
- local setup, connection format, capabilities, limits, supported server
  versions, and known exclusions are documented;
- build dependencies remain optional for consumers that do not enable the
  backend;
- [Backends](backends.md) and the CI matrix accurately reflect the verified
  support claim.

The broader project milestone criteria are listed in [Backend
portability](backend-portability.md#definition-of-done).
