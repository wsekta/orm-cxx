#include "orm-cxx/model/Schema.hpp"
#include "orm-cxx/relations.hpp"

struct Inverse;

struct Owner
{
    int id;
    orm::ManyToMany<Inverse> inverses;

    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&Owner::inverses>().through<"owner_inverses">());
};

struct Inverse
{
    int id;
    orm::ManyToMany<Owner> owners;

    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&Inverse::owners>().mappedBy<&Owner::inverses>().through<"forbidden">());
};

using BadSchema = orm::Schema<Owner, Inverse>;
constexpr auto badSchema = BadSchema::view;
