#include "tests/StandardLibrary.hpp"

import orm;

struct Target
{
    int id;
};

struct Owner
{
    orm::ManyToMany<Target> targets;
};

constexpr auto invalid = orm::manyToMany<&Owner::targets>().ownerColumns<>();
