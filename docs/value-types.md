# Date, time and UUID values

`import orm;` exports five scalar types in every backend configuration,
including core-only builds. Models, query parameters, DTO projections, keys and
migrations use these types. Database operations require a matching native
backend capability. The implementation below awaits verification of this change
in GitHub Actions; no new live-test result is claimed here.

| Type | Representation | Meaning |
| --- | --- | --- |
| `orm::Date` | `std::chrono::year_month_day` | Calendar date |
| `orm::TimeOfDay` | Validated microseconds since midnight | Time of day without a date or zone |
| `orm::LocalDateTime` | `std::chrono::local_time<std::chrono::microseconds>` | Civil date and time without a zone |
| `orm::UtcTimestamp` | `std::chrono::sys_time<std::chrono::microseconds>` | Instant on the UTC timeline |
| `orm::Uuid` | `std::array<std::byte, 16>` | UUID in canonical byte order |

## Native mappings

| C++ type | PostgreSQL | MySQL | MariaDB | SQLite |
| --- | --- | --- | --- | --- |
| `Date` | `DATE` | `DATE` | `DATE` | Unsupported |
| `TimeOfDay` | `TIME(6) WITHOUT TIME ZONE` | `TIME(6)` | `TIME(6)` | Unsupported |
| `LocalDateTime` | `TIMESTAMP(6) WITHOUT TIME ZONE` | `DATETIME(6)` | `DATETIME(6)` | Unsupported |
| `UtcTimestamp` | `TIMESTAMP(6) WITH TIME ZONE` | `TIMESTAMP(6)` | `TIMESTAMP(6)` | Unsupported |
| `Uuid` | `UUID` | Unsupported | `UUID` | Unsupported |

See [Backends](backends.md) for the server profiles. The ORM does not emulate
these types in SQLite or replace MySQL UUID with `BINARY(16)` or `CHAR(36)`.
An unsupported type, including a nullable field containing `NULL`, gives
`DatabaseErrorCode::UnsupportedFeature` before SQL.

## Models and values

Include the standard-library headers used by your application as usual. This
model requires PostgreSQL or MariaDB because its key is a native UUID:

```cpp
#include <chrono>
#include <optional>

import orm;

struct Event
{
    orm::Uuid id;
    orm::Date date;
    orm::TimeOfDay startsAt;
    orm::LocalDateTime startsLocal;
    std::optional<orm::UtcTimestamp> recordedAt;
};
using AppSchema = orm::Schema<Event>;

const orm::Date day{std::chrono::year{2026}, std::chrono::month{10}, std::chrono::day{24}};
const orm::TimeOfDay time{std::chrono::hours{9} + std::chrono::microseconds{123456}};
const orm::LocalDateTime local{std::chrono::local_days{day} + time.sinceMidnight()};
const auto id = orm::Uuid::parse("019a1122-3344-7788-99aa-bbccddeeff00");
Event event{id, day, time, local, std::nullopt};
```

`std::optional<T>` stores SQL `NULL`; non-optional fields are required. These
types can be manual or composite keys and related foreign keys on supporting
backends. `autoIncrement` remains restricted to `int`. The ORM does not generate
UUIDs, read the current time, add server defaults or add `ON UPDATE` behavior.

`TimeOfDay` defaults to midnight. Its explicit constructor accepts microseconds
in `[0, 24h)` and throws `std::invalid_argument` otherwise.
`sinceMidnight()` returns that duration. Negative durations, `24:00:00` and
leap seconds are excluded.

`Date`, `LocalDateTime` and `UtcTimestamp` are aliases. Their constructors
follow chrono rules; database operations validate before binding and after
decoding. `Date` must satisfy `.ok()`. Temporal columns have fixed microsecond
precision `(6)`, preserving values such as `.000001` and `.123456`. More
precise durations require an explicit chrono conversion in application code.

## Ranges and time zones

The public date domain is years 0001–9999. Backend limits narrow it:

| Value | PostgreSQL | MySQL | MariaDB |
| --- | --- | --- | --- |
| `Date` | `0001-01-01` through `9999-12-31` | `1000-01-01` through `9999-12-31` | Same as MySQL |
| `LocalDateTime` | Years 0001–9999, through `23:59:59.999999` | `1000-01-01 00:00:00` through `9999-12-31 23:59:59.499999` | Years 1000–9999, through `23:59:59.999999` |
| `UtcTimestamp` | Years 0001–9999 | `1970-01-01 00:00:01` through `2038-01-19 03:14:07.499999` UTC | Same lower bound; through `2038-01-19 03:14:07.999999` before 11.5, or `2106-02-07 06:28:15.999999` from 11.5 |

The MySQL fractional upper bounds are conservative limits of the ORM contract.
`BackendCapabilities::valueLimits` reports static bounds. MariaDB timestamps
also use limits negotiated from the connected server version. Reconnect repeats
that negotiation. Values outside the effective range give
`DatabaseErrorCode::Conversion`, without clipping or normalization.
Zero dates, invalid calendar dates, PostgreSQL BC and infinity values fail
conversion on read.

`LocalDateTime` stores civil fields without identifying a zone or an instant.
`UtcTimestamp` identifies an instant, but retains neither an original offset
nor an IANA zone name. Resolving daylight-saving gaps and overlaps belongs to
the application. There is no implicit local-to-UTC conversion.

PostgreSQL sessions use `DateStyle = ISO, YMD` and `TimeZone = UTC`; UTC
parameters carry an explicit offset. MySQL/MariaDB use
`time_zone = '+00:00'`, strict SQL mode and
`explicit_defaults_for_timestamp = ON`. Their timestamp parameters use that
UTC session. Settings are established again after reconnect, and migration
guards reject changes. Changing them through raw SQL violates the session
contract.

## Queries and projections

All five types support equality, inequality, `IN`, `NOT IN`, null predicates,
`COUNT`, `DISTINCT`, grouping and DTO projection. Filter parameters have the
same scalar type as the column. An optional filter uses its contained type or
an explicit null predicate.

Temporal values also support ordered comparisons, `BETWEEN`, typed
`ORDER BY`, `MIN` and `MAX`. `MIN`/`MAX` retain the scalar type; use
`std::optional<T>` for an aggregate that can return `NULL`. UUID typed
ordering, range predicates and `MIN`/`MAX` are unavailable. `SUM`, `AVG`,
`LIKE` and date arithmetic are not operations on these types.

```cpp
using namespace orm::query;

orm::Query<Event> query;
query.where(col<&Event::date>() == day)
     .andWhere(col<&Event::recordedAt>().isNull())
     .orderBy(asc(col<&Event::startsLocal>()));

constexpr auto byId = select<Event>().where(col<&Event::id>() == param<orm::Uuid, 0>());
// context.select(byId, id);
```

Mutable builders and immutable plans share these checks. Cached SQL does not
bypass backend capabilities or runtime limits. See [Queries](query.md),
[DTO projections](partial-result-queries.md) and [static plans](static-queries.md).

## UUID representation

`Uuid` defaults to nil and can be explicitly constructed from 16 bytes.
`bytes()` returns a const reference. `parse()` requires exactly the
`8-4-4-4-12` hexadecimal format, accepts uppercase and lowercase digits, and
throws `std::invalid_argument` for malformed input. `toString()` emits
lowercase hexadecimal with leading zeroes. Every 128-bit pattern is representable;
the value type does not restrict UUID versions or generation algorithms.

PostgreSQL accepts the entire value domain. MariaDB additionally rejects UUIDs
whose version is at least 8 and whose variant has its highest bit clear; binding
such a value gives `Conversion`. C++ comparisons use canonical byte order.
Native MariaDB SQL UUID ordering can differ, so it is not exposed by the typed
query API. Raw SQL retains native semantics.

See [schema migrations](schema-migrations.md) for typed constants and precision
drift detection, and [the executable example](../examples/temporal_values.cpp)
for a native UUID model with nullable UTC timestamps.
