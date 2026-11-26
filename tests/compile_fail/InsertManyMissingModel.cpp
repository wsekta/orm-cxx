#include "tests/StandardLibrary.hpp"

import orm;

namespace
{
struct Included
{
    int id;
};

struct Missing
{
    int id;
};

using AppSchema = orm::Schema<Included>;
} // namespace

auto rejected(orm::Database& database) -> void
{
    const std::vector<Missing> rows;
    database.orm<AppSchema>().insertMany(rows);
}
