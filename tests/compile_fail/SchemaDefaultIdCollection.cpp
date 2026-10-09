#include "tests/StandardLibrary.hpp"

import orm;

struct Target
{
    int id;
};

struct Owner
{
    int value;
    orm::ManyToMany<Target> id;

    inline static constexpr auto relations = orm::relations(orm::manyToMany<&Owner::id>().through<"owner_targets">());
};

using BadSchema = orm::Schema<Owner, Target>;
constexpr auto badSchema = BadSchema::view;
