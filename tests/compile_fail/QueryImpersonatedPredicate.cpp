#include <vector>

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

struct ImpersonatedPredicate
{
    using Model = User;
    inline static constexpr bool isPredicate = true;
    inline static constexpr bool writeSafe = true;
};

[[maybe_unused]] auto rejected(Database& database) -> void
{
    (void)database;
    orm::Query<User> query;
    query.where(ImpersonatedPredicate{});
}
