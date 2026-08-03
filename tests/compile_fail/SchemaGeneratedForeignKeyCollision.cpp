#include "orm-cxx/model/Schema.hpp"

struct Parent
{
    int id;
};

struct Child
{
    int id;
    int parent_id;
    Parent parent;
};

using BadSchema = orm::Schema<Parent, Child>;
constexpr auto badSchema = BadSchema::view;
