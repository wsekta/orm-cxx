#include "orm-cxx/model/Schema.hpp"
#include "orm-cxx/relations.hpp"

struct Target
{
    int id;
};

struct FirstOwner
{
    int id;
    orm::ManyToMany<Target> targets;

    inline static constexpr auto relations = orm::relations(orm::manyToMany<&FirstOwner::targets>()
                                                                .through<"shared_junction">()
                                                                .ownerColumns<"first_owner_id">()
                                                                .targetColumns<"target_id">());
};

struct SecondOwner
{
    int id;
    orm::ManyToMany<Target> targets;

    inline static constexpr auto relations = orm::relations(orm::manyToMany<&SecondOwner::targets>()
                                                                .through<"shared_junction">()
                                                                .ownerColumns<"second_owner_id">()
                                                                .targetColumns<"target_id">());
};

using BadSchema = orm::Schema<FirstOwner, SecondOwner, Target>;
constexpr auto badSchema = BadSchema::view;
