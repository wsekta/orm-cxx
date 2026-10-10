#include "tests/StandardLibrary.hpp"

import orm;

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

constexpr int User::*nullMember = nullptr;

[[maybe_unused]] auto rejected(Context& database) -> void
{
    (void)database;
    (void)col<nullMember>();
}
