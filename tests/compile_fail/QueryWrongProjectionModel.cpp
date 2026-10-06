#include <vector>

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected(Database& database) -> void
{
    (void)database;
    orm::ProjectionQuery<User, UserName> query;
    query.project(as("name", col<&Other::age>()));
}
