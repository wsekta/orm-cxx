#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "orm-cxx/database/binding/PrimaryKey.hpp"
#include "orm-cxx/database/RelationStatements.hpp"
#include "orm-cxx/query.hpp"
#include "src/database/defaults/SqlRenderer.hpp"
#include "tests/CollectionModelsDefinitions.hpp"
#include "tests/ModelsDefinitions.hpp"

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

    [[nodiscard]] auto renderCreateTablePrefix(std::string_view tableName,
                                               bool ifNotExists) const -> std::string override
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

auto collectionPredicate(std::string relation, orm::query::CollectionOperator collectionOperator,
                         orm::query::PredicateNodePtr predicate = nullptr) -> orm::query::Predicate
{
    return orm::query::Predicate{orm::query::PredicateNode{orm::query::CollectionExpression{
        .relation = std::move(relation),
        .collectionOperator = collectionOperator,
        .predicate = std::move(predicate),
    }}};
}
} // namespace

TEST(CollectionRelationCoverageTest, constEmptyCollectionIsUsable)
{
    const orm::OneToMany<int> empty;

    EXPECT_FALSE(empty.isLoaded());
    EXPECT_TRUE(empty.empty());
    EXPECT_TRUE(empty.values().empty());
    EXPECT_EQ(empty.begin(), empty.end());
}

TEST(CollectionRelationCoverageTest, queryRejectsEmptyInclude)
{
    orm::Query<collection_models::User> query;

    EXPECT_THROW((void)query.include(""), std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, primaryKeyHelpersUseStaticModelMetadata)
{
    const models::ModelWithId object{7, 11, "value"};
    EXPECT_EQ(orm::db::binding::getPrimaryKey<models::Schema>(object), key(7));

    const auto model = orm::modelView<models::Schema, models::ModelWithId>();
    const auto columns = orm::db::binding::getPrimaryKeyColumns(model);
    ASSERT_EQ(columns.size(), 1);
    EXPECT_EQ(columns[0], &model->columns[0]);
    EXPECT_EQ(columns[0]->fieldName, "id");

    EXPECT_THROW((void)orm::db::binding::toPrimaryKeyValue(std::optional<int>{}, "id"), std::invalid_argument);
    EXPECT_EQ(orm::db::binding::toPrimaryKeyValue(std::optional<int>{9}, "id"), orm::query::QueryValue{9});
    EXPECT_THROW((void)orm::db::binding::toPrimaryKeyValue(std::vector<int>{1}, "id"), std::invalid_argument);
    EXPECT_THROW((void)orm::db::binding::getPrimaryKey<models::Schema>(models::ModelWithOneField{1}),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, hydratedPrimaryKeySupportsEveryStorageCategory)
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

TEST(CollectionRelationCoverageTest, staticRelationViewsResolveTargetsAndInverseJunctions)
{
    const auto user = orm::modelView<collection_models::Schema, collection_models::User>();
    const auto role = orm::modelView<collection_models::Schema, collection_models::Role>();
    const auto& owning = user->relations.front();
    const auto& inverse = role->relations.front();

    EXPECT_EQ(user.resolveTarget(owning), role);
    EXPECT_EQ(role.resolveTarget(inverse), user);

    const auto junction = role.resolveJunction(inverse);
    EXPECT_EQ(junction.tableName, "collection_user_roles");
    ASSERT_EQ(junction.ownerColumns.size(), 1);
    ASSERT_EQ(junction.targetColumns.size(), 1);
    EXPECT_EQ(junction.ownerColumns[0], "role_id");
    EXPECT_EQ(junction.targetColumns[0], "user_id");
    EXPECT_FALSE(junction.owningSide);
}

TEST(CollectionRelationCoverageTest, relationStatementsDelegateSqlSyntaxToDialect)
{
    TrackingRelationDialect dialect;
    const auto user = orm::modelView<collection_models::Schema, collection_models::User>();
    const auto relation = user->relations.front();
    const auto junction = relation.junction;

    const auto createStatements = orm::db::relations::createTableStatements(dialect, user);
    ASSERT_EQ(createStatements.size(), 1);
    EXPECT_NE(createStatements.front().find("CREATE_RELATION_IF_ABSENT [collection_user_roles] ("), std::string::npos);
    EXPECT_NE(createStatements.front().find("[user_id] PORTABLE_TYPE NOT NULL"), std::string::npos);
    EXPECT_NE(createStatements.front().find("FOREIGN KEY ([user_id]) REFERENCES [collection_users] ([id])"),
              std::string::npos);

    const auto dropStatements = orm::db::relations::dropTableStatements(dialect, user);
    ASSERT_EQ(dropStatements.size(), 1);
    EXPECT_EQ(dropStatements.front(), "DROP_RELATION_IF_PRESENT [collection_user_roles];");

    const auto link = orm::db::relations::linkStatement(dialect, user, relation, key(1), key(10));
    EXPECT_EQ(link.sql, "INSERT_RELATION_IF_ABSENT;");
    ASSERT_TRUE(dialect.lastInsert.has_value());
    const auto expectedColumns = std::vector<std::string>{"user_id", "role_id"};
    const auto expectedValues = std::vector<std::string>{"$orm_rel_owner_0", "$orm_rel_target_0"};
    EXPECT_EQ(dialect.lastInsert->tableName, junction.tableName);
    EXPECT_EQ(dialect.lastInsert->columns, expectedColumns);
    EXPECT_EQ(dialect.lastInsert->valueExpressions, expectedValues);
    EXPECT_EQ(dialect.lastInsert->conflictColumns, expectedColumns);

    const auto unlink = orm::db::relations::unlinkStatement(dialect, user, relation, key(1), key(10));
    EXPECT_EQ(unlink.sql, "DELETE FROM [collection_user_roles] WHERE [user_id] = $orm_rel_owner_0 AND "
                          "[role_id] = $orm_rel_target_0;");

    const auto target = user.resolveTarget(relation);
    ASSERT_NE(target, nullptr);
    const auto targetSelect = std::string{"SELECT 1 AS "} + std::string{target->tableName} + "_id;";
    const auto collectionSelect =
        orm::db::relations::collectionSelectStatement(dialect, user, relation, targetSelect, {key(1)}, true);
    EXPECT_NE(collectionSelect.sql.find("[orm_relation_junction].[user_id]"), std::string::npos);
    EXPECT_NE(collectionSelect.sql.find("FROM [collection_user_roles] AS [orm_relation_junction]"), std::string::npos);
    EXPECT_NE(collectionSelect.sql.find("$orm_rel_key_0_0"), std::string::npos);

    const auto author = orm::modelView<collection_models::Schema, collection_models::Author>();
    const auto books = author->relations.front();
    const auto assignBook = orm::db::relations::linkStatement(dialect, author, books, key(1), key(10));
    EXPECT_NE(assignBook.sql.find("UPDATE [collection_books] SET [author_id] = $orm_rel_owner_0"), std::string::npos);
    const auto detachBook = orm::db::relations::unlinkStatement(dialect, author, books, key(1), key(10));
    EXPECT_NE(detachBook.sql.find("[author_id] = $orm_rel_null_0"), std::string::npos);

    orm::db::commands::RenderContext context{.model = user, .dialect = dialect};
    EXPECT_EQ(orm::db::commands::addAutomaticParameter(context, orm::query::QueryValue{1}), "$orm_p0");
    EXPECT_EQ(orm::db::commands::addNullParameter(context, orm::model::ColumnType::Int), "$orm_p1");
}

TEST(CollectionRelationCoverageTest, inverseRelationStatementsUseResolvedJunction)
{
    TrackingRelationDialect dialect;
    const auto role = orm::modelView<collection_models::Schema, collection_models::Role>();
    const auto relation = role->relations.front();

    EXPECT_TRUE(orm::db::relations::createTableStatements(dialect, role).empty());
    EXPECT_TRUE(orm::db::relations::dropTableStatements(dialect, role).empty());

    const auto link = orm::db::relations::linkStatement(dialect, role, relation, key(10), key(1));
    EXPECT_EQ(link.sql, "INSERT_RELATION_IF_ABSENT;");
    ASSERT_TRUE(dialect.lastInsert.has_value());
    EXPECT_EQ(dialect.lastInsert->columns, (std::vector<std::string>{"role_id", "user_id"}));

    const auto unlink = orm::db::relations::unlinkStatement(dialect, role, relation, key(10), key(1));
    EXPECT_EQ(unlink.sql, "DELETE FROM [collection_user_roles] WHERE [role_id] = $orm_rel_owner_0 AND "
                          "[user_id] = $orm_rel_target_0;");
}

TEST(CollectionRelationCoverageTest, relationStatementsRejectIncompleteRuntimeKeys)
{
    TrackingRelationDialect dialect;
    const auto user = orm::modelView<collection_models::Schema, collection_models::User>();
    const auto relation = user->relations.front();

    EXPECT_THROW((void)orm::db::relations::linkStatement(dialect, user, relation, {}, key(10)), std::invalid_argument);
    EXPECT_THROW((void)orm::db::relations::linkStatement(dialect, user, relation, key(1), {}), std::invalid_argument);
    EXPECT_THROW((void)orm::db::relations::unlinkStatement(dialect, user, relation, {}, key(10)),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, collectionSelectHandlesEmptyAndIncompleteRuntimeKeys)
{
    TrackingRelationDialect dialect;
    const auto user = orm::modelView<collection_models::Schema, collection_models::User>();
    const auto relation = user->relations.front();

    const auto empty = orm::db::relations::collectionSelectStatement(dialect, user, relation, "SELECT 1;", {}, true);
    EXPECT_NE(empty.sql.find("0 = 1"), std::string::npos);

    EXPECT_THROW((void)orm::db::relations::collectionSelectStatement(dialect, user, relation, "SELECT 1;", {{}}, true),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, requiredOneToManyCannotBeUnlinked)
{
    TrackingRelationDialect dialect;
    const auto author = orm::modelView<collection_models::Schema, collection_models::RequiredAuthor>();
    const auto books = author->relations.front();

    EXPECT_THROW((void)orm::db::relations::unlinkStatement(dialect, author, books, key(1), key(10)),
                 std::invalid_argument);
}

TEST(CollectionRelationCoverageTest, collectionPredicateRendererRejectsInvalidRuntimePaths)
{
    TrackingRelationDialect dialect;
    const auto user = orm::modelView<collection_models::Schema, collection_models::User>();
    orm::db::commands::RenderContext unknownRelationContext{.model = user, .dialect = dialect};

    EXPECT_THROW((void)orm::db::commands::renderWhere(
                     collectionPredicate("missing", orm::query::CollectionOperator::Exists), unknownRelationContext),
                 std::invalid_argument);

    orm::db::commands::RenderContext missingPredicateContext{.model = user, .dialect = dialect};
    EXPECT_THROW((void)orm::db::commands::renderWhere(collectionPredicate("roles", orm::query::CollectionOperator::Any),
                                                      missingPredicateContext),
                 std::invalid_argument);
}
