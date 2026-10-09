#include "tests/StandardLibrary.hpp"

import orm;

struct DuplicateModel
{
    int id;
};

using BadSchema = orm::Schema<DuplicateModel, DuplicateModel>;
constexpr auto badSchema = BadSchema::view;
