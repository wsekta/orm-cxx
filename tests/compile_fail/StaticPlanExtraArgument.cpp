#include "tests/StandardLibrary.hpp"

import orm;

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected(Database& database) -> void
{
    (void)database;
    auto plan = select<User>().where(col<&User::age>() == param<int, 0>());
    (void)plan.toDynamic(18, 21);
}
