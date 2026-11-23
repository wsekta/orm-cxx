#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <variant>

import orm;
import orm_cxx_uuid_prototype.application;

auto uuidPrototypeOtherTranslationUnit(const orm::Uuid& uuid) -> bool;
auto uuidPrototypeOtherTypeId() -> orm::model::TypeId;

static_assert(orm::UuidAdaptable<uuid_application::UserId>);
static_assert(orm::UuidAdaptable<uuid_application::OrderId>);

struct AdaptedConsumerRecord
{
    uuid_application::UserId id;
    std::optional<uuid_application::UserId> optionalId;
    orm::Uuid nativeId;
};
using AdaptedConsumerSchema = orm::Schema<AdaptedConsumerRecord>;
static_assert(orm::modelView<AdaptedConsumerSchema, AdaptedConsumerRecord>()->columns[0].type ==
              orm::model::ColumnType::Uuid);
static_assert(orm::model::logicalType<uuid_application::UserId>() == orm::model::ColumnType::Uuid);

template <typename Column>
consteval auto adaptedUuidOperators() -> bool
{
    using uuid_application::OrderId;
    using uuid_application::UserId;
    static_assert(requires(Column column, UserId value) { column == value; });
    static_assert(requires(Column column, UserId value) { column != value; });
    static_assert(requires(Column column, UserId value) { column.in(value); });
    static_assert(requires(Column column, UserId value) { column.notIn(value); });
    static_assert(requires(Column column) { count(column); });
    static_assert(!requires(Column column, OrderId value) { column == value; });
    static_assert(!requires(Column column, OrderId value) { column != value; });
    static_assert(!requires(Column column, OrderId value) { column.in(value); });
    static_assert(!requires(Column column, orm::Uuid value) { column == value; });
    static_assert(!requires(Column column, UserId value) { column < value; });
    static_assert(!requires(Column column, UserId value) { column <= value; });
    static_assert(!requires(Column column, UserId value) { column > value; });
    static_assert(!requires(Column column, UserId value) { column >= value; });
    static_assert(!requires(Column column, UserId value) { column.between(value, value); });
    static_assert(!requires(Column column, UserId value) { column.like(value); });
    static_assert(!requires(Column column) { asc(column); });
    static_assert(!requires(Column column) { desc(column); });
    static_assert(!requires(Column column) { min(column); });
    static_assert(!requires(Column column) { max(column); });
    static_assert(!requires(Column column) { sum(column); });
    static_assert(!requires(Column column) { avg(column); });
    return true;
}
static_assert(adaptedUuidOperators<decltype(orm::query::col<&AdaptedConsumerRecord::id>())>());
static_assert(requires { orm::query::col<&AdaptedConsumerRecord::optionalId>().isNull(); });
static_assert(requires { orm::query::col<&AdaptedConsumerRecord::optionalId>().isNotNull(); });

struct UserIdDto
{
    uuid_application::UserId id;
};
struct OptionalUserIdDto
{
    std::optional<uuid_application::UserId> id;
};
struct OrderIdDto
{
    uuid_application::OrderId id;
};
struct NativeUuidDto
{
    orm::Uuid id;
};

template <typename Result, typename Projection>
concept AcceptsUuidProjection =
    requires(Projection projection) { orm::query::selectAs<AdaptedConsumerRecord, Result>(projection); };

template <typename Result, typename Projection>
concept AcceptsDynamicUuidProjection = requires(orm::ProjectionQuery<AdaptedConsumerRecord, Result> query,
                                                Projection projection) { query.project(projection); };

using UserProjection = decltype(orm::query::as<"id">(orm::query::col<&AdaptedConsumerRecord::id>()));
using OptionalUserProjection = decltype(orm::query::as<"id">(orm::query::col<&AdaptedConsumerRecord::optionalId>()));
using NativeProjection = decltype(orm::query::as<"id">(orm::query::col<&AdaptedConsumerRecord::nativeId>()));
static_assert(AcceptsUuidProjection<UserIdDto, UserProjection>);
static_assert(AcceptsUuidProjection<OptionalUserIdDto, UserProjection>);
static_assert(AcceptsUuidProjection<UserIdDto, OptionalUserProjection>);
static_assert(!AcceptsUuidProjection<OrderIdDto, UserProjection>);
static_assert(!AcceptsUuidProjection<NativeUuidDto, UserProjection>);
static_assert(!AcceptsUuidProjection<UserIdDto, NativeProjection>);
static_assert(AcceptsDynamicUuidProjection<UserIdDto, UserProjection>);
static_assert(AcceptsDynamicUuidProjection<OptionalUserIdDto, UserProjection>);
static_assert(!AcceptsDynamicUuidProjection<OrderIdDto, UserProjection>);
static_assert(!AcceptsDynamicUuidProjection<NativeUuidDto, UserProjection>);

auto uuidRuntimeProjectionDomains() -> bool
{
    const auto user = orm::query::as("id", orm::query::col<&AdaptedConsumerRecord::id>());
    const auto optional = orm::query::as("id", orm::query::col<&AdaptedConsumerRecord::optionalId>());
    const auto native = orm::query::as("id", orm::query::col<&AdaptedConsumerRecord::nativeId>());
    orm::ProjectionQuery<AdaptedConsumerRecord, UserIdDto> same;
    same.project(user);
    orm::ProjectionQuery<AdaptedConsumerRecord, OptionalUserIdDto> sameOptional;
    sameOptional.project(optional.dynamic());
    const auto rejects = []<typename Result, typename Projection>(const Projection& projection)
    {
        try
        {
            orm::ProjectionQuery<AdaptedConsumerRecord, Result> query;
            query.project(projection);
            return false;
        }
        catch (const std::invalid_argument&)
        {
            return true;
        }
    };
    return rejects.template operator()<OrderIdDto>(user) && rejects.template operator()<OrderIdDto>(user.dynamic()) &&
           rejects.template operator()<NativeUuidDto>(user.dynamic()) && rejects.template operator()<UserIdDto>(native);
}

[[maybe_unused]] inline constexpr auto adaptedPlan = orm::query::select<AdaptedConsumerRecord>().where(
    orm::query::col<&AdaptedConsumerRecord::id>() == orm::query::param<uuid_application::UserId, 0>());
[[maybe_unused]] inline constexpr auto groupedAdaptedPlan =
    orm::query::select<AdaptedConsumerRecord>().groupBy(orm::query::col<&AdaptedConsumerRecord::id>()).distinct();

auto uuidPrototypeConsumer() -> bool
{
    if (!uuidPrototypeOtherTranslationUnit(orm::Uuid{}) ||
        uuidPrototypeOtherTypeId() != orm::model::typeId<uuid_application::UserId>() || !uuidRuntimeProjectionDomains())
        return false;

    // Exercise all 256 possible octets, including embedded zero bytes, while
    // preserving their canonical positions across two different domain types.
    for (unsigned int start = 0; start < 256; ++start)
    {
        std::array<std::byte, 16> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::byte>((start + index) % 256);
        const orm::Uuid uuid{bytes};
        const auto user = orm::UuidAdapter<uuid_application::UserId>::fromUuid(uuid);
        const auto order = orm::UuidAdapter<uuid_application::OrderId>::fromUuid(uuid);
        if (orm::UuidAdapter<uuid_application::UserId>::toUuid(user) != uuid ||
            orm::UuidAdapter<uuid_application::OrderId>::toUuid(order) != uuid ||
            std::get<orm::Uuid>(orm::query::QueryValue{user}.get()) != uuid ||
            std::get<orm::Uuid>(orm::query::QueryValue{std::optional{user}}.get()) != uuid ||
            !uuidPrototypeOtherTranslationUnit(uuid))
            return false;
    }
    return true;
}
