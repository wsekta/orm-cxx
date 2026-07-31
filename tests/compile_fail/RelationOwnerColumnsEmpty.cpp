#include "orm-cxx/relations.hpp"

struct Target
{
    int id;
};

struct Owner
{
    orm::ManyToMany<Target> targets;
};

constexpr auto invalid = orm::manyToMany<&Owner::targets>().ownerColumns<>();
