module;

#include <string>
#include <string_view>

export module orm:value_codec;

import :foundation_types;
import :foundation_values;

export namespace orm::detail::values
{
auto valid(Date value) noexcept -> bool;
auto valid(TimeOfDay value) noexcept -> bool;
auto valid(LocalDateTime value) noexcept -> bool;
auto valid(UtcTimestamp value) noexcept -> bool;
auto valid(const Uuid& value) noexcept -> bool;

auto validate(Date value) -> void;
auto validate(TimeOfDay value) -> void;
auto validate(LocalDateTime value) -> void;
auto validate(UtcTimestamp value) -> void;
auto validate(const Uuid& value) -> void;

auto format(Date value) -> std::string;
auto format(TimeOfDay value) -> std::string;
auto format(LocalDateTime value) -> std::string;
auto format(UtcTimestamp value, bool includeOffset = true) -> std::string;
auto format(const Uuid& value) -> std::string;

auto parseDate(std::string_view text) -> Date;
auto parseTimeOfDay(std::string_view text) -> TimeOfDay;
auto parseLocalDateTime(std::string_view text) -> LocalDateTime;
auto parseUtcTimestamp(std::string_view text) -> UtcTimestamp;

auto validateNative(Date value, db::BackendType backend) -> void;
auto validateNative(TimeOfDay value, db::BackendType backend) -> void;
auto validateNative(LocalDateTime value, db::BackendType backend) -> void;
auto validateNative(UtcTimestamp value, db::BackendType backend) -> void;
auto validateNative(const Uuid& value, db::BackendType backend) -> void;
auto validateNativeUuid(const Uuid& value, db::BackendType backend) -> void;
}
