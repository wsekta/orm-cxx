module;

#include <array>
#include <cstddef>

export module orm_cxx_uuid_prototype.application;

import orm;
export import orm_cxx_uuid_prototype.contract;

export namespace uuid_application
{
// Deliberately not layout-compatible with the UUID: the adapter must copy
// the explicitly exposed bytes, not assume the representation of the object.
struct UserId
{
    int discriminator{42};
    std::array<std::byte, 16> bytes{};
};

struct OrderId
{
    std::array<std::byte, 16> bytes{};
};

struct MissingFromUuid {};
struct ReferenceResult {};
} // namespace uuid_application

export namespace orm
{
template <>
struct UuidAdapter<uuid_application::UserId>
{
    static auto toUuid(const uuid_application::UserId& value) -> Uuid
    {
        return Uuid{value.bytes};
    }

    static auto fromUuid(const Uuid& value) -> uuid_application::UserId
    {
        return {.discriminator = 42, .bytes = value.bytes()};
    }
};

template <>
struct UuidAdapter<uuid_application::OrderId>
{
    static auto toUuid(const uuid_application::OrderId& value) -> Uuid
    {
        return Uuid{value.bytes};
    }

    static auto fromUuid(const Uuid& value) -> uuid_application::OrderId
    {
        return {.bytes = value.bytes()};
    }
};

template <>
struct UuidAdapter<uuid_application::MissingFromUuid>
{
    static auto toUuid(const uuid_application::MissingFromUuid&) -> Uuid;
};

template <>
struct UuidAdapter<uuid_application::ReferenceResult>
{
    static auto toUuid(const uuid_application::ReferenceResult&) -> const Uuid&;
    static auto fromUuid(const Uuid&) -> uuid_application::ReferenceResult;
};
} // namespace orm
