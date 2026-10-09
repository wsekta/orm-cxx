#include "tests/StandardLibrary.hpp"

import orm;

struct NullablePrimaryKey
{
    std::optional<int> id;

    inline static constexpr auto id_columns = orm::primaryKey<&NullablePrimaryKey::id>();
};

using BadSchema = orm::Schema<NullablePrimaryKey>;
constexpr auto badSchema = BadSchema::view;
