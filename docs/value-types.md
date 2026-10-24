# Date, time and UUID values

`import orm;` exports five value types in every backend configuration, including
core-only builds. They provide the foundation for native database type support.
They are not yet supported as model columns or query parameters; the current
[model field contract](model.md#supported-field-types) still applies.

| Type | Representation | Meaning |
| --- | --- | --- |
| `orm::Date` | `std::chrono::year_month_day` | Calendar date |
| `orm::TimeOfDay` | Validated microseconds since midnight | Time of day without a date or zone |
| `orm::LocalDateTime` | `std::chrono::local_time<std::chrono::microseconds>` | Civil date and time without a zone |
| `orm::UtcTimestamp` | `std::chrono::sys_time<std::chrono::microseconds>` | Instant on the UTC timeline |
| `orm::Uuid` | `std::array<std::byte, 16>` | UUID in canonical byte order |

Include the standard-library headers used by your application as usual:

```cpp
#include <chrono>
#include <string>

import orm;

const orm::Date date{std::chrono::year{2026}, std::chrono::month{10}, std::chrono::day{24}};
const orm::TimeOfDay time{std::chrono::hours{9} + std::chrono::microseconds{123456}};
const auto id = orm::Uuid::parse("019a1122-3344-7788-99aa-bbccddeeff00");
const std::string text = id.toString();
```

`TimeOfDay` defaults to midnight. Its explicit constructor accepts a microsecond
duration in the half-open range `[0, 24h)` and throws `std::invalid_argument`
outside that range. `sinceMidnight()` returns that duration. Negative durations
and `24:00:00` are not valid times of day. Construction and comparisons are
available in constant expressions.

`Date`, `LocalDateTime` and `UtcTimestamp` are aliases for the indicated chrono
types. They retain chrono's construction rules; constructing an alias does not
validate a database range. In particular, check `Date::ok()` when accepting a
calendar date. Local civil time and UTC instants are distinct types; resolving
time zones and ambiguous daylight-saving transitions belongs to the application.
Reducing a more precise duration to microseconds requires an explicit chrono
conversion.

`Uuid` defaults to the nil UUID and can be explicitly constructed from its 16
bytes. `bytes()` returns a const reference to those bytes. `parse()` accepts
exactly the `8-4-4-4-12` hexadecimal format, with uppercase or lowercase digits,
and throws `std::invalid_argument` for malformed input. `toString()` emits
lowercase hexadecimal with all leading zeroes. All 128-bit patterns are
representable, independently of version and variant; database adapters can
impose additional restrictions when native column support is implemented.

UUID comparisons in C++ use canonical byte order. They do not promise the same
order as native SQL UUID comparisons. These value types do not generate UUIDs
or read the current time.
