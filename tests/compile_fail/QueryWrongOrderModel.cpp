#include <vector>

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected(Database& database) -> void
{
    (void)database;
    orm::Query<User> query;
    query.orderBy(asc(col<&Other::age>()));
}
