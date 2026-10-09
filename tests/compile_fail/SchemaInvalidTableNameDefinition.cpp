#include "tests/StandardLibrary.hpp"

import orm;

struct Model
{
    int id;
    inline static constexpr int table_name = 7;
};

using BadSchema = orm::Schema<Model>;
constexpr auto badSchema = BadSchema::view;
