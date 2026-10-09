#include "tests/StandardLibrary.hpp"

import orm;

struct Target
{
    int id;
};

struct Owner
{
    orm::OneToMany<Target> targets;
};

constexpr auto invalid = orm::oneToMany<&Owner::targets>().mappedBy<"">();
