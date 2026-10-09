#include "tests/StandardLibrary.hpp"

import orm;

struct KeylessTarget
{
    int id;

    inline static constexpr auto id_columns = orm::primaryKey<>();
};

struct Owner
{
    int id;
    orm::ManyToMany<KeylessTarget> targets;

    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&Owner::targets>().through<"owner_targets">());
};

using BadSchema = orm::Schema<Owner, KeylessTarget>;
constexpr auto badSchema = BadSchema::view;
