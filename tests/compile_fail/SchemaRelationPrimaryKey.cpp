#include "tests/StandardLibrary.hpp"

import orm;

struct Parent
{
    int id;
};

struct Child
{
    Parent parent;

    inline static constexpr auto id_columns = orm::primaryKey<&Child::parent>();
};

using BadSchema = orm::Schema<Parent, Child>;
constexpr auto badSchema = BadSchema::view;
