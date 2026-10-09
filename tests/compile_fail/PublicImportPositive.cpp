#include "tests/StandardLibrary.hpp"

import orm;

struct PublicImportModel
{
    int id;
    std::string name;
};

using PublicImportSchema = orm::Schema<PublicImportModel>;

static_assert(orm::reflection::fieldName<PublicImportModel, 0>() == "id");
static_assert(PublicImportSchema::contains<PublicImportModel>);
static_assert(std::same_as<typename decltype(orm::query::col<&PublicImportModel::id>())::Model, PublicImportModel>);

[[maybe_unused]] auto publicImportsCompile() -> void
{
    orm::Database<PublicImportSchema> database;
    const auto plan = orm::query::select<PublicImportModel>().where(orm::query::col<&PublicImportModel::id>() ==
                                                                    orm::query::param<int, 0>());
    (void)database;
    (void)plan.toDynamic(1);
}
