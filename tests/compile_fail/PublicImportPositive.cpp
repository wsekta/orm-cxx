#include "tests/StandardLibrary.hpp"

import orm;

struct PublicUuidId
{
    int value;
};

template <>
struct orm::UuidAdapter<PublicUuidId>
{
    static auto toUuid(const PublicUuidId&) -> orm::Uuid;
    static auto fromUuid(const orm::Uuid&) -> PublicUuidId;
};

template <>
struct orm::model::LogicalTypeTraits<PublicUuidId>
{
    inline static constexpr auto value = orm::model::ColumnType::Uuid;
};

static_assert(orm::UuidAdaptable<PublicUuidId>);
static_assert(orm::model::logicalType<PublicUuidId>() == orm::model::ColumnType::Uuid);
static_assert(orm::model::logicalType<std::optional<PublicUuidId>>() == orm::model::ColumnType::Uuid);

struct PublicImportModel
{
    int id;
    std::string name;
};

using PublicImportSchema = orm::Schema<PublicImportModel>;

static_assert(orm::reflection::fieldName<PublicImportModel, 0>() == "id");
static_assert(PublicImportSchema::contains<PublicImportModel>);
static_assert(std::same_as<typename decltype(orm::query::col<&PublicImportModel::id>())::Model, PublicImportModel>);

[[maybe_unused]] auto publicImportsCompile(orm::Database& database) -> void
{
    auto context = database.orm<PublicImportSchema>();
    const auto plan = orm::query::select<PublicImportModel>().where(orm::query::col<&PublicImportModel::id>() ==
                                                                    orm::query::param<int, 0>());
    (void)database;
    (void)context;
    (void)plan.toDynamic(1);
}
