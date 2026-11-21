#include "tests/StandardLibrary.hpp"

import orm;

struct ConflictingId
{
    int value;
};

template <>
struct orm::UuidAdapter<ConflictingId>
{
    static auto toUuid(const ConflictingId&) -> orm::Uuid;
    static auto fromUuid(const orm::Uuid&) -> ConflictingId;
};

template <>
struct orm::model::LogicalTypeTraits<ConflictingId>
{
    inline static constexpr auto value = orm::model::ColumnType::String;
};

struct Model
{
    int id;
    std::optional<ConflictingId> value;
};

using BadSchema = orm::Schema<Model>;
constexpr auto badSchema = BadSchema::view;
