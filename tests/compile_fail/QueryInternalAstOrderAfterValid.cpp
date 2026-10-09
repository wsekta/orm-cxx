module;

#include "tests/StandardLibrary.hpp"

module orm;

import :internal;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected() -> void
{
    orm::Query<User> query;
    query.orderBy(asc(col<&User::age>()), detail::rawOrder("missing ASC"));
}
