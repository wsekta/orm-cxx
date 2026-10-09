module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

using namespace orm::test::fixtures;

namespace
{
struct OwnerTag
{
};

struct TargetTag
{
};

struct OtherTag
{
};

constexpr auto scalarColumn() -> orm::model::ColumnView
{
    return {
        .fieldIndex = 0, .fieldName = "id", .name = "id", .type = orm::model::ColumnType::Int, .isPrimaryKey = true};
}

constexpr auto relatedColumn(std::size_t targetIndex) -> orm::model::ColumnView
{
    return {.fieldIndex = 0,
            .fieldName = "target",
            .name = "target",
            .kind = orm::model::FieldKind::ToOne,
            .targetModelIndex = targetIndex};
}

constexpr auto collectionRelation(std::size_t targetIndex) -> orm::model::RelationView
{
    return {.fieldIndex = 1,
            .fieldName = "targets",
            .columnName = "targets",
            .kind = orm::model::RelationKind::ManyToMany,
            .targetModelIndex = targetIndex,
            .junction = {.tableName = "owner_targets", .owningSide = true}};
}

struct SyntheticMetadata
{
    const std::array<orm::model::ColumnView, 1> ownerColumns;
    const std::array<orm::model::ColumnView, 1> targetColumns{scalarColumn()};
    const std::array<std::size_t, 1> primaryKeyIndices{0};
    const std::array<orm::model::RelationView, 1> ownerRelations;
    const orm::model::ModelDataView ownerData;
    const orm::model::ModelDataView targetData;
    const std::array<const orm::model::ModelDataView*, 2> modelPointers;
    const orm::model::SchemaView schema;

    SyntheticMetadata(orm::model::ColumnView column, orm::model::RelationView relation)
        : ownerColumns{column},
          ownerRelations{relation},
          ownerData{.type = orm::model::typeId<OwnerTag>(),
                    .schemaIndex = 0,
                    .tableName = "owners",
                    .columns = ownerColumns,
                    .primaryKeyIndices = primaryKeyIndices,
                    .relations = ownerRelations},
          targetData{.type = orm::model::typeId<TargetTag>(),
                     .schemaIndex = 1,
                     .tableName = "targets",
                     .columns = targetColumns,
                     .primaryKeyIndices = primaryKeyIndices},
          modelPointers{&ownerData, &targetData},
          schema{modelPointers}
    {
    }

    [[nodiscard]] auto owner() const -> orm::model::ModelView
    {
        return schema.at(0);
    }

    [[nodiscard]] auto target() const -> orm::model::ModelView
    {
        return schema.at(1);
    }
};

template <typename Operation>
auto expectInvalidArgument(Operation&& operation, std::string_view message) -> void
{
    try
    {
        (void)std::forward<Operation>(operation)();
        FAIL() << "Expected std::invalid_argument";
    }
    catch (const std::invalid_argument& error)
    {
        EXPECT_EQ(error.what(), message);
    }
}

template <typename Operation>
auto expectUnsupportedFeature(Operation&& operation, std::string_view expectedOperation,
                              std::string_view expectedMessage) -> void
{
    try
    {
        (void)std::forward<Operation>(operation)();
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Sqlite);
        EXPECT_EQ(error.getOperation(), expectedOperation);
        EXPECT_EQ(error.what(), expectedMessage);
    }
}

TEST(DatabaseValidationTest, MissingColumnAndRelationTargetsKeepDistinctErrors)
{
    const SyntheticMetadata metadata{relatedColumn(orm::model::noTargetModel),
                                     collectionRelation(orm::model::noTargetModel)};

    expectInvalidArgument([&] { return orm::detail::requireToOneTarget(metadata.owner(), metadata.ownerColumns[0]); },
                          "To-one column has no target model in the schema");
    expectInvalidArgument([&]
                          { return orm::detail::requireRelationTarget(metadata.owner(), metadata.ownerRelations[0]); },
                          "Collection relation has no target model in the schema");

    const SyntheticMetadata valid{relatedColumn(1), collectionRelation(1)};
    EXPECT_EQ(orm::detail::requireToOneTarget(valid.owner(), valid.ownerColumns[0]), valid.target());
    EXPECT_EQ(orm::detail::requireRelationTarget(valid.owner(), valid.ownerRelations[0]), valid.target());
}

TEST(DatabaseValidationTest, EndpointKeyMustMatchPrimaryKeyColumns)
{
    const SyntheticMetadata metadata{scalarColumn(), collectionRelation(1)};
    expectInvalidArgument([&] { return orm::detail::requireEndpointKeyColumns(metadata.owner(), {}); },
                          "Incomplete relation endpoint primary key");

    const orm::db::binding::PrimaryKey key{orm::query::QueryValue{42}};
    const auto columns = orm::detail::requireEndpointKeyColumns(metadata.owner(), key);
    ASSERT_EQ(columns.size(), 1U);
    EXPECT_EQ(columns[0], &metadata.ownerColumns[0]);
}

TEST(DatabaseValidationTest, OnlyOwningConfiguredManyToManyRelationsAreSelected)
{
    const SyntheticMetadata metadata{scalarColumn(), collectionRelation(1)};
    const orm::model::RelationView oneToMany{.kind = orm::model::RelationKind::OneToMany,
                                             .targetModelIndex = 1,
                                             .junction = {.tableName = "owner_targets", .owningSide = true}};
    const orm::model::RelationView unconfigured{.kind = orm::model::RelationKind::ManyToMany, .targetModelIndex = 1};
    const orm::model::RelationView inverse{.kind = orm::model::RelationKind::ManyToMany,
                                           .targetModelIndex = 1,
                                           .junction = {.tableName = "owner_targets", .owningSide = false}};

    const std::array relations{oneToMany, unconfigured, inverse, collectionRelation(1)};
    const orm::model::ModelDataView ownerData{.type = metadata.ownerData.type,
                                              .schemaIndex = 0,
                                              .tableName = "owners",
                                              .columns = metadata.ownerColumns,
                                              .primaryKeyIndices = metadata.primaryKeyIndices,
                                              .relations = relations};
    const std::array<const orm::model::ModelDataView*, 2> modelPointers{&ownerData, &metadata.targetData};
    const orm::model::SchemaView schema{modelPointers};
    const auto selected = orm::detail::owningJunctionRelations(schema.at(0));
    ASSERT_EQ(selected.size(), 1U);
    EXPECT_EQ(selected[0], &relations[3]);
    EXPECT_EQ(orm::detail::requireOwningJunctionTarget(schema.at(0), *selected[0]), schema.at(1));

    const SyntheticMetadata missing{scalarColumn(), collectionRelation(orm::model::noTargetModel)};
    expectInvalidArgument(
        [&] { return orm::detail::requireOwningJunctionTarget(missing.owner(), missing.ownerRelations[0]); },
        "ManyToMany relation target is outside the selected schema");
}

TEST(DatabaseValidationTest, OwningJunctionDetectionUsesRelationMetadataAtRuntime)
{
    const SyntheticMetadata owner{scalarColumn(), collectionRelation(1)};
    const SyntheticMetadata withoutJunction{scalarColumn(),
                                            {.kind = orm::model::RelationKind::ManyToMany, .targetModelIndex = 1}};
    const SyntheticMetadata inverse{scalarColumn(),
                                    {.kind = orm::model::RelationKind::ManyToMany,
                                     .targetModelIndex = 1,
                                     .junction = {.tableName = "owner_targets", .owningSide = false}}};

    EXPECT_TRUE(orm::detail::hasOwningJunction(owner.owner()));
    EXPECT_FALSE(orm::detail::hasOwningJunction(withoutJunction.owner()));
    EXPECT_FALSE(orm::detail::hasOwningJunction(inverse.owner()));
}

TEST(DatabaseValidationTest, ModelAndIncludeChecksPreserveDatabaseErrorContext)
{
    const SyntheticMetadata missing{relatedColumn(orm::model::noTargetModel),
                                    collectionRelation(orm::model::noTargetModel)};
    expectUnsupportedFeature(
        [&]
        {
            return orm::detail::requireSupportedRelatedTarget(missing.owner(), missing.ownerColumns[0],
                                                              orm::db::BackendType::Sqlite, "create table");
        },
        "create table", "A related model is outside the selected schema");
    expectUnsupportedFeature(
        [&]
        {
            return orm::detail::requireIncludedRelationTarget(missing.owner(), missing.ownerRelations[0],
                                                              orm::db::BackendType::Sqlite);
        },
        "include collection", "An included relation target is outside the selected schema");

    const SyntheticMetadata valid{relatedColumn(1), collectionRelation(1)};
    EXPECT_EQ(orm::detail::requireSupportedRelatedTarget(valid.owner(), valid.ownerColumns[0],
                                                         orm::db::BackendType::Sqlite, "create table"),
              valid.target());
    EXPECT_EQ(orm::detail::requireIncludedRelationTarget(valid.owner(), valid.ownerRelations[0],
                                                         orm::db::BackendType::Sqlite),
              valid.target());
}

TEST(DatabaseValidationTest, CollectionWrapperTargetMustMatchMetadataType)
{
    const SyntheticMetadata missing{scalarColumn(), collectionRelation(orm::model::noTargetModel)};
    const SyntheticMetadata wrongType{scalarColumn(), collectionRelation(1)};

    expectInvalidArgument(
        [&]
        {
            return orm::detail::requireCollectionTarget(missing.owner(), missing.ownerRelations[0],
                                                        orm::model::typeId<TargetTag>());
        },
        "Collection wrapper target does not match relation metadata: targets");
    expectInvalidArgument(
        [&]
        {
            return orm::detail::requireCollectionTarget(wrongType.owner(), wrongType.ownerRelations[0],
                                                        orm::model::typeId<OtherTag>());
        },
        "Collection wrapper target does not match relation metadata: targets");
    EXPECT_EQ(orm::detail::requireCollectionTarget(wrongType.owner(), wrongType.ownerRelations[0],
                                                   orm::model::typeId<TargetTag>()),
              wrongType.target());
}
} // namespace
