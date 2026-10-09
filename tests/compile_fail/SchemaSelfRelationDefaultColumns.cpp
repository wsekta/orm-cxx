#include "tests/StandardLibrary.hpp"

import orm;

struct SelfRelated
{
    int id;
    orm::ManyToMany<SelfRelated> peers;

    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&SelfRelated::peers>().through<"self_links">());
};

using BadSchema = orm::Schema<SelfRelated>;
constexpr auto badSchema = BadSchema::view;
