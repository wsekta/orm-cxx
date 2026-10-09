#include "tests/StandardLibrary.hpp"

import orm;

struct Unsupported
{
};

struct Model
{
    int id;
    Unsupported value;
};

using BadSchema = orm::Schema<Model>;
constexpr auto badSchema = BadSchema::view;
