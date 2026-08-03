#include "orm-cxx/model/Schema.hpp"
#include "orm-cxx/relations.hpp"

struct Target
{
    int id;
};

struct Owner
{
    int id;
    orm::ManyToMany<Target> items;

    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&Owner::items>();
    inline static constexpr auto relations = orm::relations(orm::manyToMany<&Owner::items>().through<"owner_items">());
};

using BadSchema = orm::Schema<Owner, Target>;
constexpr auto badSchema = BadSchema::view;
