#include "tests/StandardLibrary.hpp"

import orm;

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected(Context& database) -> void
{
    (void)database;
    (void)(col<&User::age>() == param<int, 0>()).dynamic();
}
