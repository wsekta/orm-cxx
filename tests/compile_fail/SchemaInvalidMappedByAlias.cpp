#include <optional>

#include "orm-cxx/model/Schema.hpp"
#include "orm-cxx/relations.hpp"

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
