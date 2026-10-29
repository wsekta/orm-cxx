#include <array>
#include <cstddef>

import orm;
import orm_cxx_uuid_prototype.application;

auto uuidPrototypeOtherTranslationUnit(const orm::Uuid& uuid) -> bool;

static_assert(orm::UuidAdaptable<uuid_application::UserId>);
static_assert(orm::UuidAdaptable<uuid_application::OrderId>);

auto uuidPrototypeConsumer() -> bool
{
    if (!uuidPrototypeOtherTranslationUnit(orm::Uuid{}))
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
            !uuidPrototypeOtherTranslationUnit(uuid))
            return false;
    }
    return true;
}
