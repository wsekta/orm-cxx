# Date, time and UUID values

`import orm;` exports five scalar types in every backend configuration,
including core-only builds. Models, query parameters, DTO projections, keys and
migrations use these types. Database operations require a supported mapping for
the selected backend; UUID fields can also explicitly select Binary16 or
Text36 storage.

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

See [Backends](backends.md) for the server profiles. Native UUID is the default;
the ORM does not silently replace it with `BINARY(16)` or `CHAR(36)`.
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

## Custom converters

`UuidAdapter<T>` remains the explicit UUID-specific contract. Unrelated
third-party values use a separate `ValueConverter<T>` specialization. It
declares the canonical ORM value it produces, and the ORM derives the logical
column type from that value:

```cpp
import orm;

struct Money
{
    long long minorUnits;
};

template <>
struct orm::ValueConverter<Money>
{
    using Value = long long;
    static auto toValue(const Money& money) -> long long { return money.minorUnits; }
    static auto fromValue(const long long& value) -> Money { return Money{value}; }
};

static_assert(orm::ValueConvertible<Money>);
static_assert(orm::model::logicalType<Money>() == orm::model::ColumnType::LongLong);
```

The canonical value must be one of the scalar types the ORM already transports,
including `orm::Uuid`, the temporal types, `std::string` and the numeric types.
The converter is used by model fields, DTO projections, primary-key extraction
and typed query parameters. It is opt-in and independent of the built-in
scalar support and of `UuidAdapter`. The ORM does not depend on a third-party
library; the application owns every specialization.

Third-party nullable wrappers such as `boost::optional<T>` use
`NullableAdapter<T>`. With it, the wrapper behaves like `std::optional<T>` for
model fields and DTO projections:

```cpp
#include <boost/optional.hpp>

namespace orm {
template <typename T>
struct NullableAdapter<boost::optional<T>>
{
    using Value = T;
    static auto hasValue(const boost::optional<T>& value) noexcept -> bool { return bool{value}; }
    static auto value(const boost::optional<T>& source) -> const T& { return *source; }
    static auto make(T value) -> boost::optional<T> { return boost::optional<T>{value}; }
    static auto none() -> boost::optional<T> { return boost::none; }
};
}
```

`std::optional<T>` already uses the same nullable contract, so application
wrappers compose with `ValueConverter`, `UuidAdapter` and the built-in scalar
types. A converter that throws `std::invalid_argument` is a value rejection: it
becomes a `Conversion` error during a database operation without exposing the
exception text. Other converter exceptions propagate unchanged.

## UUID adapters and explicit UUID representations

An application UUID type provides an explicit specialization of
`orm::UuidAdapter<T>`. Both functions return values of the exact types shown;
`orm::UuidAdaptable<T>` checks this contract. The adapter converts through the
canonical `orm::Uuid` value, rather than the foreign type's object layout:

```cpp
import orm;

struct UserId
{
    orm::Uuid value;
};

template <>
struct orm::UuidAdapter<UserId>
{
    static auto toUuid(const UserId& id) -> orm::Uuid { return id.value; }
    static auto fromUuid(const orm::Uuid& id) -> UserId { return UserId{id}; }
};

static_assert(orm::UuidAdaptable<UserId>);
```

The application owns the specialization, including any integration with a
third-party UUID library; the ORM does not depend on Boost. The ORM handles
`std::optional<T>` nulls before calling the adapter and keeps
distinct domain types such as `UserId` and `OrderId` distinct in typed queries.
Adapter rejection through `std::invalid_argument` becomes a `Conversion`
error during a database operation without exposing the value or
the adapter's exception text. A standalone `QueryValue` construction retains
the original exception. Other exceptions, including resource failures, are not
conversion refusals.

The optional `BoostConverterTest` integration test requires
`ORM_CXX_BUILD_TESTS=ON`, `ORM_CXX_ENABLE_SQLITE_BACKEND=ON`, and Boost headers
(`boost/uuid/uuid.hpp` and `boost/optional.hpp`). CMake searches its standard
include paths and `Boost_ROOT` / `BOOST_ROOT` (including the `BOOST_ROOT`
environment variable), and reports whether this test is enabled during
configuration. For an installation outside those paths, configure with
`-DORM_CXX_BOOST_INCLUDE_DIR=<directory-containing-boost>`. Boost is not
downloaded for this test; when its headers are unavailable, this test is
omitted and the converter tests using application types still run.

Model hydration currently default-constructs the model and assigns its fields;
model fields therefore need to support those operations. A non-optional UUID
field in a DTO has the same construction and assignment requirements. For an
optional adapted UUID field in a DTO, the adapter's value is constructed only
for a non-NULL result; the adapted type need not be default-constructible or
assignable, but it must be movable or copyable into the returned DTO.

The opt-in is per field and backend. For example, after defining the adapter
above, model metadata can declare its storage as follows:

```cpp
struct User
{
    UserId id;
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&User::id, orm::db::BackendType::Mysql,
                         orm::model::UuidStorage::Binary16>(),
        orm::uuidStorage<&User::id, orm::db::BackendType::Sqlite,
                         orm::model::UuidStorage::Binary16>());
};
```

The physical types are MySQL `BINARY(16)` and SQLite `BLOB` containing
exactly 16 bytes in canonical order, including zero bytes. PostgreSQL and
MariaDB retain native UUID. Related foreign and junction keys inherit the
mapping of their target keys.

Engines without a native UUID can instead select the canonical text
representation with `UuidStorage::Text36`:

```cpp
struct User
{
    UserId id;
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&User::id, orm::db::BackendType::Mysql, orm::model::UuidStorage::Text36>(),
        orm::uuidStorage<&User::id, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Text36>());
};
```

The physical types are MySQL `CHAR(36)` and SQLite `TEXT`, containing exactly
the lowercase `8-4-4-4-12` text. SQLite reads validate both the actual storage
class and the canonical text. No automatic fallback, UUID byte swapping, UUID
generation, general-purpose blob API or other implicit representation is
provided. Binary16 and Text36 are still logical UUIDs: their physical storage
does not enable typed ordering, range predicates, arithmetic or string
operations. The application must declare `Text36` explicitly; it is never
chosen for a UUID field by default.

Existing data needs an explicit, reviewed transformation when its physical UUID
representation changes; changing only the C++ wrapper does not change the
schema. SQLite reads validate both the actual storage class and byte length.
MySQL `BINARY(16)` pads short values with zeroes, so a value read after server
normalization does not establish that the original input had 16 bytes.

Raw SQL fragments do not provide a model column from which to infer a UUID
representation. Specify it for every UUID parameter in the fragment:

```cpp
using namespace orm::query;
const UserId id{orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff")};
orm::Query<User> query;
query.where(raw<User>("id = :id", param("id", id, orm::model::UuidStorage::Binary16)));

// For NULL, provide both the logical type and the representation.
auto nullId = param("id", std::nullopt, orm::model::ColumnType::Uuid,
                    orm::model::UuidStorage::Binary16);
```

Alternatively, pass an `std::optional<UserId>` and the representation; the
empty optional binds SQL `NULL`. The same applies to `UuidStorage::Text36`.
Raw UUID parameters without an explicit representation are rejected, as are
contradictory declarations for the same placeholder. Raw SQL must use a
representation compatible with the active backend.

See [schema migrations](schema-migrations.md) for typed constants and precision
drift detection, and [the executable example](../examples/temporal_values.cpp)
for a native UUID model with nullable UTC timestamps.
