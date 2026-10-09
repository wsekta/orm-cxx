#include "tests/StandardLibrary.hpp"

import orm;

struct Child;

struct Parent
{
    int id;
    orm::OneToMany<Child> children;

    inline static constexpr auto relations =
        orm::relations(orm::oneToMany<&Parent::children>().mappedBy<"parent_fk">());
};

struct Child
{
    int id;
    std::optional<Parent> parent;

    inline static constexpr auto columns_names = orm::columnNames(orm::columnName<&Child::parent, "parent_fk">());
};

using BadSchema = orm::Schema<Parent, Child>;
constexpr auto badSchema = BadSchema::view;
