#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected() -> void
{
    orm::Query<User> query;
    query.groupBy(col<&User::name>(), col<&Other::age>());
}
