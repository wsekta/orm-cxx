#include "orm-cxx/relations.hpp"

struct Target
{
    int id;
};

struct WrongTarget
{
    int owner;
};

struct Owner
{
    orm::OneToMany<Target> targets;
};

constexpr auto invalid = orm::oneToMany<&Owner::targets>().mappedBy<&WrongTarget::owner>();
