#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_set>
#include <utility>
#include <vector>

#include "orm-cxx/database/binding/PrimaryKey.hpp"
#include "orm-cxx/database/RelationStatements.hpp"
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
class TrackingRelationDialect final : public orm::db::SqlDialect
{
public:
    [[nodiscard]] auto quoteIdentifier(std::string_view identifier) const -> std::string override
    {
        return "[" + std::string{identifier} + "]";
    }

    [[nodiscard]] auto bindMarker(std::string_view logicalName) const -> std::string override
    {
        return "$" + std::string{logicalName};
    }

    [[nodiscard]] auto toSqlType(orm::model::ColumnType /*type*/) const -> std::string override
    {
        return "PORTABLE_TYPE";
    }

    [[nodiscard]] auto renderCreateTablePrefix(std::string_view tableName, bool ifNotExists) const
        -> std::string override
    {
        return std::string{ifNotExists ? "CREATE_RELATION_IF_ABSENT " : "CREATE_RELATION "} +
               quoteIdentifier(tableName) + " (";
    }

    [[nodiscard]] auto renderDropTable(std::string_view tableName, bool ifExists) const -> std::string override
    {
        return std::string{ifExists ? "DROP_RELATION_IF_PRESENT " : "DROP_RELATION "} + quoteIdentifier(tableName) +
               ";";
    }

    [[nodiscard]] auto renderAutoIncrementPrimaryKey(std::string_view columnName) const -> std::string override
    {
        return "AUTO " + quoteIdentifier(columnName);
    }

    [[nodiscard]] auto renderPagination(const orm::db::PaginationSpec& /*pagination*/) const -> std::string override
    {
        return {};
    }

    [[nodiscard]] auto renderInsertIfAbsent(const orm::db::InsertIfAbsentSpec& insert) const -> std::string override
    {
        lastInsert = insert;
        return "INSERT_RELATION_IF_ABSENT;";
    }

    mutable std::optional<orm::db::InsertIfAbsentSpec> lastInsert;
};

auto key(int value) -> orm::db::binding::PrimaryKey
{
    return {orm::query::QueryValue{value}};
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
    EXPECT_EQ(orm::db::binding::toPrimaryKeyValue(std::optional<int>{7}, "id"), orm::query::QueryValue{7});
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
    values.set("boolean", 1);
    values.set("integer", 7);
    values.set("unsigned", static_cast<unsigned long long>(8));
    values.set("signed", static_cast<long long>(9));
    values.set("floating", 10.5);
    values.set("text", std::string{"key"});
    values.set("null_value", 0);
    values.set("null_value", 0, soci::i_null);

    EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "boolean", orm::model::ColumnType::Bool),
              orm::query::QueryValue{true});
    for (const auto type :
         {orm::model::ColumnType::Char, orm::model::ColumnType::UnsignedChar, orm::model::ColumnType::Short,
          orm::model::ColumnType::UnsignedShort, orm::model::ColumnType::Int})
    {
        EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "integer", type),
                  orm::query::QueryValue::fromStorage(type, orm::query::QueryValue::Value{7}));
    }
    for (const auto type : {orm::model::ColumnType::UnsignedInt, orm::model::ColumnType::UnsignedLongLong})
    {
        EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "unsigned", type),
                  orm::query::QueryValue::fromStorage(
                      type, orm::query::QueryValue::Value{static_cast<unsigned long long>(8)}));
    }
    EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "signed", orm::model::ColumnType::LongLong),
              orm::query::QueryValue{static_cast<long long>(9)});
    for (const auto type : {orm::model::ColumnType::Float, orm::model::ColumnType::Double})
    {
        EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "floating", type),
                  orm::query::QueryValue::fromStorage(type, orm::query::QueryValue::Value{10.5}));
    }
    EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "text", orm::model::ColumnType::String),
              orm::query::QueryValue{std::string{"key"}});
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

    EXPECT_THROW(orm::model::detail::validateJunctionColumnNames({""}, "roles", "owner"), std::invalid_argument);
    EXPECT_THROW(orm::model::detail::validateJunctionColumnNames({"duplicate", "duplicate"}, "roles", "owner"),
                 std::invalid_argument);
    EXPECT_THROW(((void)orm::model::detail::makeOwningJunction<Owner, Target>(
                     orm::manyToMany("roles").through(Owner::table_name))),
                 std::invalid_argument);
    EXPECT_THROW(((void)orm::model::detail::makeOwningJunction<Owner, Target>(
                     orm::manyToMany("roles").through("coverage_target_count").targetColumns({"a", "b"}))),
                 std::invalid_argument);
    EXPECT_THROW(((void)orm::model::detail::makeOwningJunction<Owner, Target>(orm::manyToMany("roles")
                                                                                  .through("coverage_colliding_columns")
                                                                                  .ownerColumns({"same"})
                                                                                  .targetColumns({"same"}))),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, owningJunctionDefaultsEndpointColumnNames)
{
    const auto junction =
        orm::model::detail::makeOwningJunction<collection_models::Member, collection_models::Permission>(
            orm::manyToMany("permissions").through("coverage_default_columns"));

    EXPECT_EQ(junction.ownerColumns, std::vector<std::string>{"collection_members_id"});
    EXPECT_EQ(junction.targetColumns, std::vector<std::string>{"collection_permissions_id"});
}

TEST(CollectionRelationCoverageTest, mappedByAndInverseMetadataValidationRejectInvalidDescriptors)
{
    EXPECT_THROW(
        ((void)orm::model::detail::validateOneToManyMappedBy<collection_models::Author, collection_models::Book>(
            orm::oneToMany("books"))),
        std::invalid_argument);
    EXPECT_THROW(((void)orm::model::detail::makeInverseJunction<collection_models::Role, collection_models::User>(
                     orm::manyToMany("users").mappedBy("roles").through("forbidden"))),
                 std::invalid_argument);
    EXPECT_THROW(((void)orm::model::detail::makeInverseJunction<collection_models::Role, collection_models::User>(
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
    EXPECT_THROW(orm::model::detail::validateDescriptors<coverage_models::ScalarDescriptor>({}), std::invalid_argument);
    EXPECT_THROW(orm::model::detail::validateDescriptors<coverage_models::NoDescriptors>({"ghost"}),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, relationStatementsDelegateSqlSyntaxToDialect)
{
    TrackingRelationDialect dialect;
    const auto userInfo = orm::Model<collection_models::User>::getModelInfo();
    const auto& relation = userInfo.relationsInfo.front();
    const auto& junction = relation.junction.value();

    const auto createStatements = orm::db::relations::createTableStatements(dialect, userInfo);
    ASSERT_EQ(createStatements.size(), 1);
    EXPECT_NE(createStatements.front().find("CREATE_RELATION_IF_ABSENT [collection_user_roles] ("), std::string::npos);
    EXPECT_NE(createStatements.front().find("[user_id] PORTABLE_TYPE NOT NULL"), std::string::npos);
    EXPECT_NE(createStatements.front().find("FOREIGN KEY ([user_id]) REFERENCES [collection_users] ([id])"),
              std::string::npos);

    const auto dropStatements = orm::db::relations::dropTableStatements(dialect, userInfo);
    ASSERT_EQ(dropStatements.size(), 1);
    EXPECT_EQ(dropStatements.front(), "DROP_RELATION_IF_PRESENT [collection_user_roles];");

    const auto link = orm::db::relations::linkStatement(dialect, userInfo, relation, key(1), key(10));
    EXPECT_EQ(link.sql, "INSERT_RELATION_IF_ABSENT;");
    ASSERT_TRUE(dialect.lastInsert.has_value());
    const auto expectedColumns = std::vector<std::string>{"user_id", "role_id"};
    const auto expectedValues = std::vector<std::string>{"$orm_rel_owner_0", "$orm_rel_target_0"};
    EXPECT_EQ(dialect.lastInsert->tableName, junction.tableName);
    EXPECT_EQ(dialect.lastInsert->columns, expectedColumns);
    EXPECT_EQ(dialect.lastInsert->valueExpressions, expectedValues);
    EXPECT_EQ(dialect.lastInsert->conflictColumns, expectedColumns);

    const auto unlink = orm::db::relations::unlinkStatement(dialect, userInfo, relation, key(1), key(10));
    EXPECT_EQ(unlink.sql, "DELETE FROM [collection_user_roles] WHERE [user_id] = $orm_rel_owner_0 AND [role_id] = "
                          "$orm_rel_target_0;");

    const auto targetSelect = std::string{"SELECT 1 AS "} + std::string{relation.targetModel().tableName} + "_id;";
    const auto collectionSelect =
        orm::db::relations::collectionSelectStatement(dialect, userInfo, relation, targetSelect, {key(1)}, true);
    EXPECT_NE(collectionSelect.sql.find("[orm_relation_junction].[user_id]"), std::string::npos);
    EXPECT_NE(collectionSelect.sql.find("FROM [collection_user_roles] AS [orm_relation_junction]"), std::string::npos);
    EXPECT_NE(collectionSelect.sql.find("$orm_rel_key_0_0"), std::string::npos);

    const auto authorInfo = orm::Model<collection_models::Author>::getModelInfo();
    const auto& books = authorInfo.relationsInfo.front();
    const auto assignBook = orm::db::relations::linkStatement(dialect, authorInfo, books, key(1), key(10));
    EXPECT_NE(assignBook.sql.find("UPDATE [collection_books] SET [author_id] = $orm_rel_owner_0"), std::string::npos);
    const auto detachBook = orm::db::relations::unlinkStatement(dialect, authorInfo, books, key(1), key(10));
    EXPECT_NE(detachBook.sql.find("[author_id] = $orm_rel_null_0"), std::string::npos);

    orm::db::commands::RenderContext parameterContext{.modelInfo = userInfo, .dialect = dialect};
    EXPECT_EQ(orm::db::commands::addAutomaticParameter(parameterContext, orm::query::QueryValue{1}), "$orm_p0");
    EXPECT_EQ(orm::db::commands::addNullParameter(parameterContext, orm::model::ColumnType::Int), "$orm_p1");
}

TEST(CollectionRelationCoverageTest, relationStatementValidationRejectsMalformedMetadataAndKeys)
{
    auto userInfo = orm::Model<collection_models::User>::getModelInfo();
    auto roleInfo = orm::Model<collection_models::Role>::getModelInfo();
    auto userRelation = userInfo.relationsInfo.front();

    EXPECT_TRUE(orm::db::relations::createTableStatements(roleInfo).empty());
    EXPECT_TRUE(orm::db::relations::dropTableStatements(roleInfo).empty());

    auto invalidDdlInfo = userInfo;
    invalidDdlInfo.relationsInfo.front().junction->ownerColumns.clear();
    EXPECT_THROW((void)orm::db::relations::createTableStatements(invalidDdlInfo), std::invalid_argument);

    EXPECT_THROW((void)orm::db::relations::linkStatement(userInfo, userRelation, {}, key(10)), std::invalid_argument);
    EXPECT_THROW((void)orm::db::relations::linkStatement(userInfo, userRelation, key(1), {}), std::invalid_argument);

    auto missingJunction = userRelation;
    missingJunction.junction.reset();
    EXPECT_THROW((void)orm::db::relations::linkStatement(userInfo, missingJunction, key(1), key(10)),
                 std::invalid_argument);
    EXPECT_THROW((void)orm::db::relations::unlinkStatement(userInfo, missingJunction, key(1), key(10)),
                 std::invalid_argument);

    auto toOne = userRelation;
    toOne.kind = orm::model::RelationKind::ToOne;
    EXPECT_THROW((void)orm::db::relations::linkStatement(userInfo, toOne, key(1), key(10)), std::invalid_argument);
    EXPECT_THROW((void)orm::db::relations::unlinkStatement(userInfo, toOne, key(1), key(10)), std::invalid_argument);
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

    EXPECT_THROW((void)orm::db::relations::linkStatement(authorInfo, relation, key(1), key(10)), std::invalid_argument);
    EXPECT_THROW((void)orm::db::relations::unlinkStatement(authorInfo, relation, key(1), key(10)),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, collectionPredicateRendererRejectsMalformedMetadata)
{
    TrackingRelationDialect dialect;
    auto userInfo = orm::Model<collection_models::User>::getModelInfo();
    orm::db::commands::RenderContext unknownRelationContext{.modelInfo = userInfo, .dialect = dialect};

    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("missing", orm::query::CollectionOperator::Exists), unknownRelationContext),
                 std::invalid_argument);

    orm::db::commands::RenderContext missingPredicateContext{.modelInfo = userInfo, .dialect = dialect};
    EXPECT_THROW((void)orm::db::commands::renderWhere(collectionPredicate("roles", orm::query::CollectionOperator::Any),
                                                      missingPredicateContext),
                 std::invalid_argument);

    auto noJunction = userInfo;
    noJunction.relationsInfo.front().junction.reset();
    orm::db::commands::RenderContext noJunctionContext{.modelInfo = noJunction, .dialect = dialect};
    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("roles", orm::query::CollectionOperator::Exists), noJunctionContext),
                 std::invalid_argument);

    auto wrongJunction = userInfo;
    wrongJunction.relationsInfo.front().junction->targetColumns.clear();
    orm::db::commands::RenderContext wrongJunctionContext{.modelInfo = wrongJunction, .dialect = dialect};
    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("roles", orm::query::CollectionOperator::Exists), wrongJunctionContext),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, collectionPredicateRendererRejectsInvalidEndpointAndMappedByMetadata)
{
    TrackingRelationDialect dialect;
    auto userInfo = orm::Model<collection_models::User>::getModelInfo();
    userInfo.columnsInfo.front().isForeignModel = true;
    orm::db::commands::RenderContext foreignKeyContext{.modelInfo = userInfo, .dialect = dialect};
    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("roles", orm::query::CollectionOperator::Exists), foreignKeyContext),
                 std::invalid_argument);

    userInfo = orm::Model<collection_models::User>::getModelInfo();
    userInfo.columnsInfo.front().isPrimaryKey = false;
    userInfo.idColumnsNames.clear();
    orm::db::commands::RenderContext keylessContext{.modelInfo = userInfo, .dialect = dialect};
    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("roles", orm::query::CollectionOperator::Exists), keylessContext),
                 std::invalid_argument);

    auto authorInfo = orm::Model<collection_models::Author>::getModelInfo();
    authorInfo.relationsInfo.front().mappedBy = "missing";
    orm::db::commands::RenderContext invalidMappedByContext{.modelInfo = authorInfo, .dialect = dialect};
    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("books", orm::query::CollectionOperator::Exists), invalidMappedByContext),
                 std::invalid_argument);
}
