# Backend portability

`orm-cxx` remains SQLite-first and also provides an optional PostgreSQL backend.
Its portability layer requires live behavior verification instead of treating
an enum value or a generated SQL string as proof of backend support.

This document defines the portability boundary. See [Backends](backends.md) for
the support status visible to users and [Backend extension
contract](backend-extension.md) for the contributor-facing implementation
requirements.

## Portability layers

Database support is split into three layers:

1. **Public ORM behavior** covers connection lifecycle, schema operations,
   binding and hydration, CRUD, affected-row reporting, transactions, queries,
   and relations. Reusable backend conformance tests define this layer.
2. **Backend adapter** splits into a dialect for database-specific SQL and a
   runtime for session behavior. Identifier quoting, pagination syntax,
   generated-key DDL, and idempotent relation mutations belong to the dialect;
   session initialization, catalog access, limits, and driver errors belong to
   the runtime.
3. **SOCI transport** opens the driver session and exchanges prepared statement
   values. SOCI is transport infrastructure; using a SOCI backend does not by
   itself make the ORM behavior portable or supported.

Backend selection, capabilities, and limits must be resolved before an ORM
operation generates SQL. Core code must not accumulate scattered checks such as
`if (backend == ...)`.

## Portable behavior

A backend advertised as supported must pass the common conformance profile for
the public behavior it advertises. The core profile includes:

- connecting, disconnecting, and reconnecting with deterministic errors;
- table creation and removal for supported model shapes;
- prepared binding and hydration of supported scalar and nullable values;
- insert, full-model and projection select, filtered update, and filtered
  remove;
- exact affected-row counts where the public API returns a count;
- commit and rollback behavior;
- deterministic ordering and pagination;
- primary keys, composite keys, mapped names, and supported relations;
- consistent error categories independent of vendor message text.

Tests that compare rows positionally must request an explicit order. Generated
keys are tested for uniqueness and usability, not for a vendor-specific starting
value. Backend-specific SQL snapshots are dialect tests rather than conformance
tests.

## Capabilities and dialect differences

A capability describes user-visible behavior that a backend may genuinely be
unable to provide. A dialect difference describes how an advertised behavior is
implemented.

| Capability or limit | Dialect detail |
| --- | --- |
| Database-generated integer keys | Generated-column syntax |
| Full-model grouping semantics | `GROUP BY` rendering or an equivalent query |
| Collection-relation support | Junction DDL and idempotent link syntax |
| Enforced foreign keys | Session setup such as SQLite pragmas |
| Native date, time, or UUID types | Concrete SQL type name |
| Maximum bind-parameter count | How placeholders are written |
| Maximum lossless numeric exchange value | Driver representation used for binding |

Capabilities and numeric limits are reported centrally by the selected backend.
If an optional feature is unavailable, the ORM must reject it predictably before
issuing SQL. Syntax differences such as quoting, `IF EXISTS`, upsert forms, or
offset-without-limit are responsibilities of the dialect and should not require
application feature checks.

## Binding and SOCI

The ORM defines the C++ values and null semantics accepted by its public API.
Each backend must prove through conformance tests that its SOCI driver can bind
and hydrate those values without silent narrowing or null loss. A backend may
adapt SQL types or exchange representations internally, but those details must
not leak into model or query code.

Connection-string parsing, session initialization, affected-row normalization,
and driver exception translation are also backend responsibilities. Connection
strings and credentials must not be copied into public error messages.

## Raw SQL

Raw predicates and raw ordering are intentional backend-specific escape hatches.
Their fragments are passed to the selected database dialect; they are not
rewritten or promised to work on another backend. Explicit values still use
prepared SOCI bind parameters, so changing backend does not permit interpolation
of untrusted values.

Portable application code should prefer the typed query API. Documentation and
tests must label raw SQL examples with the backend whose syntax they use.

## Verification model

The test suite should have three distinct layers:

- unit and dialect tests may inspect generated SQL and binding payloads;
- common conformance tests use only public ORM APIs;
- backend-specific integration tests may inspect driver setup, catalogs, and
  vendor-specific behavior.

Every backend runs the same conformance sources through its own test harness.
The harness supplies its connection, declared capabilities and limits, isolated
schema, and cleanup. An unsupported optional capability is tested by asserting a
stable unsupported-feature error, not by silently skipping all coverage.

## Minimum CI matrix

The minimum project matrix is deliberately smaller than the Cartesian product
of every compiler and every database:

| Job | Environment | Required checks |
| --- | --- | --- |
| GCC core | Ubuntu, GCC 13, SQLite | Unit, SQLite conformance, consumer test |
| Clang coverage | Ubuntu, Clang 18, SQLite | The core checks and coverage export |
| MSVC core | Windows, MSVC, SQLite | Unit, SQLite conformance, consumer test |
| Quality and docs | Ubuntu, Clang 18, SQLite and PostgreSQL client libraries | Formatting, static analysis for both adapters, documentation validation |
| PostgreSQL 15 | Ubuntu, GCC 13, real PostgreSQL service | Unit, dialect, common conformance, live integration |
| PostgreSQL 18 | Ubuntu, Clang 18, real PostgreSQL service | Unit, dialect, common conformance, live integration, coverage |

A backend receives additional operating-system or compiler jobs only when those
combinations are publicly supported. A claimed server-version range must be
represented at its boundaries in CI; otherwise documentation must name only the
version actually verified.

## Definition of Done

The Mid Term portability milestone is complete when:

- SQLite passes reusable conformance tests on the supported GCC, Clang, and MSVC
  workflows;
- common conformance sources contain no SQLite catalog queries, pragmas, exact
  SQL snapshots, or private access to a SOCI session;
- capabilities and limits are obtained from one backend contract rather than
  scattered backend enum branches;
- command generation, binding, session setup, affected rows, and errors have
  documented backend responsibilities;
- adding a backend requires its adapter, dependency target, test harness,
  registry entry, and documentation entry, without copying common tests or
  changing public ORM semantics;
- CI verifies each advertised backend against a real database implementation;
- [Backends](backends.md) accurately distinguishes supported, experimental, and
  planned implementations.
