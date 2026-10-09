#include "tests/StandardLibrary.hpp"

import orm;

struct DuplicateColumn
{
    int id;
    int value;

    inline static constexpr auto columns_names = orm::columnNames(
        orm::columnName<&DuplicateColumn::id, "duplicate">(), orm::columnName<&DuplicateColumn::value, "duplicate">());
};

using BadSchema = orm::Schema<DuplicateColumn>;
constexpr auto badSchema = BadSchema::view;
