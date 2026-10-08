#include <array>
#include <vector>

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected(Database& database) -> void
{
    (void)database;
    auto plan = select<User>().where(col<&User::age>().in(param<std::vector<int>, 0>()));
    (void)plan.toDynamic(std::array<int, 2>{18, 21});
}
