#include <array>
#include <vector>

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected(Database& database) -> void
{
    (void)database;
    (void)remove<User>().where(col<&User::profile, &Profile::city>() == "Paris");
}
