#include "orm-cxx/model/Schema.hpp"
#include "orm-cxx/relations.hpp"

struct MissingTarget
{
    int id;
};

struct Owner
{
    int id;
    orm::ManyToMany<MissingTarget> targets;

    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&Owner::targets>().through<"owner_targets">());
};

using BadSchema = orm::Schema<Owner>;
constexpr auto badSchema = BadSchema::view;
