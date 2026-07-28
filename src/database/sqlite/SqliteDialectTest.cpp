#include "orm-cxx/database/sqlite/SqliteDialect.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

namespace
{
const orm::db::sqlite::SqliteDialect dialect;
}

TEST(SqliteDialectTest, quotesIdentifiersAndBindMarkers)
{
    EXPECT_EQ(dialect.quoteIdentifier("order"), "\"order\"");
    EXPECT_EQ(dialect.quoteIdentifier("odd\"name"), "\"odd\"\"name\"");
    EXPECT_EQ(dialect.bindMarker("value"), ":value");
    EXPECT_THROW((void)dialect.quoteIdentifier(""), std::invalid_argument);
    EXPECT_THROW((void)dialect.quoteIdentifier(std::string{"before"} + '\0' + "after"), std::invalid_argument);
    EXPECT_THROW((void)dialect.bindMarker(""), std::invalid_argument);
    EXPECT_THROW((void)dialect.bindMarker(":value"), std::invalid_argument);
    EXPECT_THROW((void)dialect.bindMarker("odd-name"), std::invalid_argument);
    EXPECT_THROW((void)dialect.bindMarker("odd name"), std::invalid_argument);
    EXPECT_THROW((void)dialect.bindMarker("odd:name"), std::invalid_argument);
    EXPECT_THROW((void)dialect.bindMarker(std::string{"before"} + '\0' + "after"), std::invalid_argument);
}

TEST(SqliteDialectTest, rendersSqliteDdlPrimitives)
{
    EXPECT_EQ(dialect.toSqlType(orm::model::ColumnType::Int), "INTEGER");
    EXPECT_EQ(dialect.toSqlType(orm::model::ColumnType::String), "TEXT");
    EXPECT_EQ(dialect.renderCreateTablePrefix("order", true), "CREATE TABLE IF NOT EXISTS \"order\" (");
    EXPECT_EQ(dialect.renderCreateTablePrefix("order", false), "CREATE TABLE \"order\" (");
    EXPECT_EQ(dialect.renderDropTable("order", true), "DROP TABLE IF EXISTS \"order\";");
    EXPECT_EQ(dialect.renderDropTable("order", false), "DROP TABLE \"order\";");
    EXPECT_EQ(dialect.renderAutoIncrementPrimaryKey("id"), "\"id\" INTEGER PRIMARY KEY AUTOINCREMENT");
    EXPECT_THROW((void)dialect.toSqlType(orm::model::ColumnType::Uuid), std::runtime_error);
}

TEST(SqliteDialectTest, rendersEveryPaginationCombination)
{
    EXPECT_EQ(dialect.renderPagination({}), "");
    EXPECT_EQ(dialect.renderPagination({.limit = 10}), " LIMIT 10");
    EXPECT_EQ(dialect.renderPagination({.offset = 5}), " LIMIT -1 OFFSET 5");
    EXPECT_EQ(dialect.renderPagination({.limit = 10, .offset = 5}), " LIMIT 10 OFFSET 5");
}

TEST(SqliteDialectTest, rendersAtomicInsertIfAbsent)
{
    const orm::db::InsertIfAbsentSpec insert{
        .tableName = "user_roles",
        .columns = {"user_id", "role_id"},
        .valueExpressions = {":user", ":role"},
        .conflictColumns = {"user_id", "role_id"},
    };

    EXPECT_EQ(dialect.renderInsertIfAbsent(insert),
              "INSERT INTO \"user_roles\" (\"user_id\", \"role_id\") VALUES (:user, :role) ON CONFLICT "
              "(\"user_id\", \"role_id\") DO NOTHING;");
}

TEST(SqliteDialectTest, rejectsIncompleteInsertIfAbsentSpecifications)
{
    EXPECT_THROW((void)dialect.renderInsertIfAbsent({}), std::invalid_argument);
    EXPECT_THROW(
        (void)dialect.renderInsertIfAbsent(
            {.tableName = "links", .columns = {"owner_id"}, .valueExpressions = {}, .conflictColumns = {"owner_id"}}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)dialect.renderInsertIfAbsent(
            {.tableName = "links", .columns = {"owner_id"}, .valueExpressions = {":owner"}, .conflictColumns = {}}),
        std::invalid_argument);
}
