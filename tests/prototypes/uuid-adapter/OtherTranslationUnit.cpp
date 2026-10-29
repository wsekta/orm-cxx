#include <array>
#include <cstddef>

import orm;
import orm_cxx_uuid_prototype.application;

static_assert(orm::UuidAdaptable<uuid_application::UserId>);
static_assert(orm::UuidAdaptable<uuid_application::OrderId>);
static_assert(!orm::UuidAdaptable<uuid_application::MissingFromUuid>);
static_assert(!orm::UuidAdaptable<uuid_application::ReferenceResult>);
static_assert(!orm::UuidAdaptable<int>);

auto uuidPrototypeOtherTranslationUnit(const orm::Uuid& uuid) -> bool
{
    const auto value = orm::UuidAdapter<uuid_application::UserId>::fromUuid(uuid);
    return value.discriminator == 42 && orm::UuidAdapter<uuid_application::UserId>::toUuid(value) == uuid;
}
