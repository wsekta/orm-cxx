#include <array>
#include <vector>

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected(Database& database) -> void
{
    (void)database;
    (void)select<User>().orderBy(asc(col<&Other::age>()));
}
