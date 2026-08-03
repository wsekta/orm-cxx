#include "orm-cxx/model/Schema.hpp"
#include "orm-cxx/relations.hpp"

struct Target
{
    int id;
};

struct Owner
{
    int id;
    orm::ManyToMany<Target> targets;
};

using BadSchema = orm::Schema<Owner, Target>;
constexpr auto badSchema = BadSchema::view;
