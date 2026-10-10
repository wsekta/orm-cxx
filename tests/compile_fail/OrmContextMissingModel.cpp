#include "tests/StandardLibrary.hpp"

import orm;

struct Included
{
    int id;
};

struct Missing
{
    int id;
};

auto rejected(orm::Database& database) -> void
{
    database.orm<orm::Schema<Included>>().insert(Missing{});
}
