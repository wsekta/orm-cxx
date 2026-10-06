#include <vector>

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected(Database& database) -> void
{
    (void)database;
    (void)database.remove<User>((col<&User::id>() == 1) || (col<&User::profile, &Profile::city>() == "city"));
}
