#include "tests/StandardLibrary.hpp"

import orm;

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected(Context& database) -> void
{
    (void)database;
    (void)remove<User>().where(col<&User::profile, &Profile::city>() == "Paris");
}
