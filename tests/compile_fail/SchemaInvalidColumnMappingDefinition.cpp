#include "tests/StandardLibrary.hpp"

import orm;

struct Model
{
    int id;
    inline static constexpr int columns_names = 7;
};

using BadSchema = orm::Schema<Model>;
constexpr auto badSchema = BadSchema::view;
