#include "tests/StandardLibrary.hpp"

import orm;

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected() -> void
{
    orm::ProjectionQuery<User, UserName> query;
    query.orderBy(asc(col<&User::age>()), desc(col<&Other::age>()));
}
