#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <typeindex>
#include <unordered_set>
#include <utility>
#include <vector>

#include "orm-cxx/database/RelationStatements.hpp"
#include "orm-cxx/database/binding/PrimaryKey.hpp"
#include "orm-cxx/model.hpp"
#include "orm-cxx/model/RelationMetadata.hpp"
#include "orm-cxx/query.hpp"
#include "src/database/defaults/SqlRenderer.hpp"
#include "tests/CollectionModelsDefinitions.hpp"
#include "tests/ModelsDefinitions.hpp"

namespace coverage_models
{
struct ExplicitlyKeyless
{
    inline static const std::vector<std::string> id_columns{};
    int id;
};

struct HoldsKeyless
{
    int id;
    ExplicitlyKeyless child;
};

struct EmptyDescriptorName
{
    int id;
    inline static const auto relations = orm::relations(orm::manyToMany("").through("coverage_empty_name"));
};

struct ScalarDescriptor
{
    int id;
    inline static const auto relations = orm::relations(orm::manyToMany("id").through("coverage_scalar"));
};

struct NoDescriptors
{
    int id;
};

struct InverseCurrent
{
    int id;
};

struct BrokenInverseTarget
{
    int id;
    orm::ManyToMany<InverseCurrent> currents;

    inline static const auto relations = orm::relations(orm::oneToMany("currents").mappedBy("missing"));
};
} // namespace coverage_models

namespace
{
auto key(int value) -> orm::db::binding::PrimaryKey
{
    return {orm::query::QueryValue{value}.get()};
}

auto primaryColumn(std::string name = "id") -> orm::model::ColumnInfo
{
    return orm::model::ColumnInfo{.fieldName = name,
                                  .name = std::move(name),
                                  .type = orm::model::ColumnType::Int,
                                  .isPrimaryKey = true,
                                  .isForeignModel = false,
                                  .isAutoIncrement = false,
                                  .isUnique = false,
                                  .isNotNull = true};
}

auto collectionPredicate(std::string relation, orm::query::CollectionOperator collectionOperator,
                         orm::query::PredicateNodePtr predicate = nullptr) -> orm::query::Predicate
{
    return orm::query::Predicate{orm::query::PredicateNode{orm::query::CollectionExpression{
        .relation = std::move(relation), .collectionOperator = collectionOperator, .predicate = std::move(predicate)}}};
}
} // namespace

TEST(CollectionRelationCoverageTest, descriptorVectorOverloadsAndConstEmptyCollectionAreUsable)
{
    const orm::OneToMany<int> empty;
    EXPECT_TRUE(empty.values().empty());

    const auto descriptor = orm::manyToMany("items")
                                .ownerColumns(std::vector<std::string>{"owner_a", "owner_b"})
                                .targetColumns(std::vector<std::string>{"target_a", "target_b"});
    EXPECT_EQ(descriptor.ownerColumnNames(), (std::vector<std::string>{"owner_a", "owner_b"}));
    EXPECT_EQ(descriptor.targetColumnNames(), (std::vector<std::string>{"target_a", "target_b"}));
}

TEST(CollectionRelationCoverageTest, queryRejectsEmptyInclude)
{
    orm::Query<collection_models::User> query;
    EXPECT_THROW((void)query.include(""), std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, primaryKeyHelpersRejectInvalidValuesAndMetadata)
{
    EXPECT_THROW((void)orm::db::binding::toPrimaryKeyValue(std::optional<int>{}, "id"), std::invalid_argument);
    EXPECT_EQ(orm::db::binding::toPrimaryKeyValue(std::optional<int>{7}, "id"),
              orm::query::QueryValue{7}.get());
    EXPECT_THROW((void)orm::db::binding::toPrimaryKeyValue(std::vector<int>{1}, "id"), std::invalid_argument);
    EXPECT_THROW((void)orm::db::binding::getPrimaryKey(models::ModelWithOneField{1}), std::invalid_argument);

    orm::model::ModelInfo foreignPrimaryKey;
    auto foreignColumn = primaryColumn();
    foreignColumn.isForeignModel = true;
    foreignPrimaryKey.columnsInfo.push_back(foreignColumn);
    foreignPrimaryKey.idColumnsNames.insert("id");
    EXPECT_THROW((void)orm::db::binding::getPrimaryKeyColumns(foreignPrimaryKey), std::invalid_argument);

    orm::model::ModelInfo keyless;
    keyless.columnsInfo.push_back(orm::model::ColumnInfo{});
    EXPECT_THROW((void)orm::db::binding::getPrimaryKeyColumns(keyless), std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, hydratedPrimaryKeySupportsEveryStorageCategoryAndRejectsInvalidValues)
{
    soci::values values;
    values.set("integer", 7);
    values.set("unsigned", static_cast<unsigned long long>(8));
    values.set("signed", static_cast<long long>(9));
    values.set("floating", 10.5);
    values.set("text", std::string{"key"});
    values.set("null_value", 0);
    values.set("null_value", 0, soci::i_null);

    for (const auto type : {orm::model::ColumnType::Bool, orm::model::ColumnType::Char,
                            orm::model::ColumnType::UnsignedChar, orm::model::ColumnType::Short,
                            orm::model::ColumnType::UnsignedShort, orm::model::ColumnType::Int})
    {
        EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "integer", type),
                  orm::query::QueryValue{7}.get());
    }
    for (const auto type : {orm::model::ColumnType::UnsignedInt, orm::model::ColumnType::UnsignedLongLong})
    {
        EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "unsigned", type),
                  orm::query::QueryValue{static_cast<unsigned long long>(8)}.get());
    }
    EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "signed", orm::model::ColumnType::LongLong),
              orm::query::QueryValue{static_cast<long long>(9)}.get());
    for (const auto type : {orm::model::ColumnType::Float, orm::model::ColumnType::Double})
    {
        EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "floating", type),
                  orm::query::QueryValue{10.5}.get());
    }
    EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "text", orm::model::ColumnType::String),
              orm::query::QueryValue{std::string{"key"}}.get());
    EXPECT_THROW((void)orm::db::binding::getPrimaryKeyValue(values, "null_value", orm::model::ColumnType::Int),
                 std::runtime_error);
    EXPECT_THROW((void)orm::db::binding::getPrimaryKeyValue(values, "integer", orm::model::ColumnType::Uuid),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, relationResolverAndKeylessNestedModelDefensivePathsAreCovered)
{
    orm::model::RelationInfo relation;
    EXPECT_THROW((void)relation.targetModel(), std::logic_error);

    const auto info = orm::model::getModelInfo<coverage_models::HoldsKeyless>();
    ASSERT_EQ(info.columnsInfo.size(), 2);
    EXPECT_FALSE(info.columnsInfo[1].isForeignModel);
    EXPECT_TRUE(info.relationsInfo.empty());
}

TEST(CollectionRelationCoverageTest, junctionMetadataValidationRejectsEveryInvalidShape)
{
    using Owner = collection_models::User;
    using Target = collection_models::Role;

    EXPECT_THROW(orm::model::detail::validateJunctionColumnNames({""}, "roles", "owner"),
                 std::invalid_argument);
    EXPECT_THROW(orm::model::detail::validateJunctionColumnNames({"duplicate", "duplicate"}, "roles", "owner"),
                 std::invalid_argument);
    EXPECT_THROW(((void)orm::model::detail::makeOwningJunction<Owner, Target>(
                     orm::manyToMany("roles").through(Owner::table_name))),
                 std::invalid_argument);
    EXPECT_THROW(((void)orm::model::detail::makeOwningJunction<Owner, Target>(
                     orm::manyToMany("roles").through("coverage_target_count").targetColumns({"a", "b"}))),
                 std::invalid_argument);
    EXPECT_THROW(((void)orm::model::detail::makeOwningJunction<Owner, Target>(
                     orm::manyToMany("roles")
                         .through("coverage_colliding_columns")
                         .ownerColumns({"same"})
                         .targetColumns({"same"}))),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, mappedByAndInverseMetadataValidationRejectInvalidDescriptors)
{
    EXPECT_THROW(((void)orm::model::detail::validateOneToManyMappedBy<collection_models::Author,
                                                                     collection_models::Book>(
                     orm::oneToMany("books"))),
                 std::invalid_argument);
    EXPECT_THROW(((void)orm::model::detail::makeInverseJunction<collection_models::Role,
                                                               collection_models::User>(
                     orm::manyToMany("users").mappedBy("roles").through("forbidden"))),
                 std::invalid_argument);
    EXPECT_THROW(((void)orm::model::detail::makeInverseJunction<collection_models::Role,
                                                               collection_models::User>(
                     orm::manyToMany("users").mappedBy("name"))),
                 std::invalid_argument);
    EXPECT_THROW(((void)orm::model::detail::makeInverseJunction<coverage_models::InverseCurrent,
                                                               coverage_models::BrokenInverseTarget>(
                     orm::manyToMany("broken").mappedBy("currents"))),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, descriptorSetValidationRejectsNamesAndMissingDescriptors)
{
    EXPECT_THROW(orm::model::detail::validateDescriptors<coverage_models::EmptyDescriptorName>({}),
                 std::invalid_argument);
    EXPECT_THROW(orm::model::detail::validateDescriptors<coverage_models::ScalarDescriptor>({}),
                 std::invalid_argument);
    EXPECT_THROW(orm::model::detail::validateDescriptors<coverage_models::NoDescriptors>({"ghost"}),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, relationStatementValidationRejectsMalformedMetadataAndKeys)
{
    auto userInfo = orm::Model<collection_models::User>::getModelInfo();
    auto roleInfo = orm::Model<collection_models::Role>::getModelInfo();
    auto userRelation = userInfo.relationsInfo.front();

    EXPECT_TRUE(orm::db::relations::createTableStatements(roleInfo).empty());

    auto invalidDdlInfo = userInfo;
    invalidDdlInfo.relationsInfo.front().junction->ownerColumns.clear();
    EXPECT_THROW((void)orm::db::relations::createTableStatements(invalidDdlInfo), std::invalid_argument);

    EXPECT_THROW((void)orm::db::relations::linkStatement(userInfo, userRelation, {}, key(10)),
                 std::invalid_argument);
    EXPECT_THROW((void)orm::db::relations::linkStatement(userInfo, userRelation, key(1), {}),
                 std::invalid_argument);

    auto missingJunction = userRelation;
    missingJunction.junction.reset();
    EXPECT_THROW((void)orm::db::relations::linkStatement(userInfo, missingJunction, key(1), key(10)),
                 std::invalid_argument);
    EXPECT_THROW((void)orm::db::relations::unlinkStatement(userInfo, missingJunction, key(1), key(10)),
                 std::invalid_argument);

    auto toOne = userRelation;
    toOne.kind = orm::model::RelationKind::ToOne;
    EXPECT_THROW((void)orm::db::relations::linkStatement(userInfo, toOne, key(1), key(10)),
                 std::invalid_argument);
    EXPECT_THROW((void)orm::db::relations::unlinkStatement(userInfo, toOne, key(1), key(10)),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, collectionSelectStatementRejectsMalformedInputs)
{
    const auto userInfo = orm::Model<collection_models::User>::getModelInfo();
    auto relation = userInfo.relationsInfo.front();

    const auto empty = orm::db::relations::collectionSelectStatement(userInfo, relation, "SELECT 1;", {}, true);
    EXPECT_NE(empty.sql.find("0 = 1"), std::string::npos);

    EXPECT_THROW((void)orm::db::relations::collectionSelectStatement(userInfo, relation, "SELECT 1;", {{}}, true),
                 std::invalid_argument);

    auto toOne = relation;
    toOne.kind = orm::model::RelationKind::ToOne;
    toOne.junction.reset();
    EXPECT_THROW((void)orm::db::relations::collectionSelectStatement(userInfo, toOne, "SELECT 1;", {key(1)}, true),
                 std::invalid_argument);

    relation.junction->targetColumns.clear();
    EXPECT_THROW((void)orm::db::relations::collectionSelectStatement(userInfo, relation, "SELECT 1;", {key(1)}, true),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, oneToManyStatementsRejectMissingMappedRelation)
{
    const auto authorInfo = orm::Model<collection_models::Author>::getModelInfo();
    auto relation = authorInfo.relationsInfo.front();
    relation.mappedBy = "missing";

    EXPECT_THROW((void)orm::db::relations::linkStatement(authorInfo, relation, key(1), key(10)),
                 std::invalid_argument);
    EXPECT_THROW((void)orm::db::relations::unlinkStatement(authorInfo, relation, key(1), key(10)),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, collectionPredicateRendererRejectsMalformedMetadata)
{
    auto userInfo = orm::Model<collection_models::User>::getModelInfo();
    orm::db::commands::RenderContext unknownRelationContext{.modelInfo = userInfo};

    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("missing", orm::query::CollectionOperator::Exists), unknownRelationContext),
                 std::invalid_argument);

    orm::db::commands::RenderContext missingPredicateContext{.modelInfo = userInfo};
    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("roles", orm::query::CollectionOperator::Any), missingPredicateContext),
                 std::invalid_argument);

    auto noJunction = userInfo;
    noJunction.relationsInfo.front().junction.reset();
    orm::db::commands::RenderContext noJunctionContext{.modelInfo = noJunction};
    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("roles", orm::query::CollectionOperator::Exists), noJunctionContext),
                 std::invalid_argument);

    auto wrongJunction = userInfo;
    wrongJunction.relationsInfo.front().junction->targetColumns.clear();
    orm::db::commands::RenderContext wrongJunctionContext{.modelInfo = wrongJunction};
    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("roles", orm::query::CollectionOperator::Exists), wrongJunctionContext),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, collectionPredicateRendererRejectsInvalidEndpointAndMappedByMetadata)
{
    auto userInfo = orm::Model<collection_models::User>::getModelInfo();
    userInfo.columnsInfo.front().isForeignModel = true;
    orm::db::commands::RenderContext foreignKeyContext{.modelInfo = userInfo};
    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("roles", orm::query::CollectionOperator::Exists), foreignKeyContext),
                 std::invalid_argument);

    userInfo = orm::Model<collection_models::User>::getModelInfo();
    userInfo.columnsInfo.front().isPrimaryKey = false;
    userInfo.idColumnsNames.clear();
    orm::db::commands::RenderContext keylessContext{.modelInfo = userInfo};
    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("roles", orm::query::CollectionOperator::Exists), keylessContext),
                 std::invalid_argument);

    auto authorInfo = orm::Model<collection_models::Author>::getModelInfo();
    authorInfo.relationsInfo.front().mappedBy = "missing";
    orm::db::commands::RenderContext invalidMappedByContext{.modelInfo = authorInfo};
    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("books", orm::query::CollectionOperator::Exists), invalidMappedByContext),
                 std::invalid_argument);
}
