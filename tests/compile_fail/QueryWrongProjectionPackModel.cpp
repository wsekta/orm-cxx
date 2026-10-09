#include "tests/StandardLibrary.hpp"

import orm;

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected() -> void
{
    orm::ProjectionQuery<User, UserName> query;
    query.project(as("name", col<&User::name>()), as("age", col<&Other::age>()));
}
