#include "tests/StandardLibrary.hpp"

import orm;

struct Target
{
    int id;
};

struct Owner
{
    int id;
    orm::ManyToMany<Target> targets;

    inline static constexpr auto relations = orm::relations(orm::manyToMany<&Owner::targets>());
};

using BadSchema = orm::Schema<Owner, Target>;
constexpr auto badSchema = BadSchema::view;
