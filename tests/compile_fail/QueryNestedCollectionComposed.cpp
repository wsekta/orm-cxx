#include "tests/StandardLibrary.hpp"

import orm;

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected(Database& database) -> void
{
    (void)database;
    (void)any<&User::roles>((col<&Role::id>() == 1) && exists<&Role::users>());
}
