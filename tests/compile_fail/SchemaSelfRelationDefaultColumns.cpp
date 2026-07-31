#include "orm-cxx/model/Schema.hpp"
#include "orm-cxx/relations.hpp"

struct SelfRelated
{
    int id;
    orm::ManyToMany<SelfRelated> peers;

    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&SelfRelated::peers>().through<"self_links">());
};

using BadSchema = orm::Schema<SelfRelated>;
constexpr auto badSchema = BadSchema::view;
