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

    inline static constexpr auto relations = orm::relations(
        orm::manyToMany<&Owner::targets>().through<"owner_targets">().targetColumns<"target_id", "extra_target_id">());
};

using BadSchema = orm::Schema<Owner, Target>;
constexpr auto badSchema = BadSchema::view;
