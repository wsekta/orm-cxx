#include "tests/StandardLibrary.hpp"

import orm;

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected(Database& database) -> void
{
    (void)database;
    orm::Update<User> update;
    update.set(col<&User::age>(), std::nullopt);
}
