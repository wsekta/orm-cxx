#include "tests/StandardLibrary.hpp"

import orm;

struct InvalidTemporalAutoIncrement
{
    orm::Uuid id;
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&InvalidTemporalAutoIncrement::id>();
};

using BadSchema = orm::Schema<InvalidTemporalAutoIncrement>;
constexpr auto badSchema = BadSchema::view;
