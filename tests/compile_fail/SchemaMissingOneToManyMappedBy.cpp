#include "orm-cxx/model/Schema.hpp"
#include "orm-cxx/relations.hpp"

struct Child;

struct Parent
{
    int id;
    orm::OneToMany<Child> children;

    inline static constexpr auto relations = orm::relations(orm::oneToMany<&Parent::children>());
};

struct Child
{
    int id;
    Parent parent;
};

using BadSchema = orm::Schema<Parent, Child>;
constexpr auto badSchema = BadSchema::view;
