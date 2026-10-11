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

TEST(MysqlDialectTest, rendersNativeDdlQuotingPaginationAndExactAggregates)
{
    const orm::db::mysql::MysqlDialect dialect;
    EXPECT_EQ(dialect.quoteIdentifier("a:b`c"), "`a:b``c`");
    EXPECT_THROW((void)dialect.quoteIdentifier(""), std::invalid_argument);
    EXPECT_THROW((void)dialect.quoteIdentifier(std::string{"x"} + '\0'), std::invalid_argument);
    EXPECT_NO_THROW((void)dialect.quoteIdentifier(std::string(64, 'x')));
    EXPECT_THROW((void)dialect.quoteIdentifier(std::string(65, 'x')), std::invalid_argument);
    EXPECT_EQ(dialect.bindMarker("value"), ":value");
    EXPECT_THROW((void)dialect.bindMarker("bad-name"), std::invalid_argument);
    EXPECT_EQ(dialect.toSqlType(orm::model::ColumnType::String, 12), "VARCHAR(12)");
    EXPECT_EQ(dialect.renderCreateTablePrefix("order", true), "CREATE TABLE IF NOT EXISTS `order` (");
    EXPECT_EQ(dialect.renderCreateTablePrefix("order", false), "CREATE TABLE `order` (");
    EXPECT_EQ(dialect.renderCreateTableSuffix(),
              "\n) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_bin;");
    EXPECT_EQ(dialect.renderDefaultInsert("order"), "INSERT INTO `order` () VALUES ();");
    EXPECT_EQ(dialect.renderDropTable("order", true), "DROP TABLE IF EXISTS `order`;");
    EXPECT_EQ(dialect.renderDropTable("order", false), "DROP TABLE `order`;");
    EXPECT_EQ(dialect.renderAutoIncrementPrimaryKey("id"), "`id` INT NOT NULL AUTO_INCREMENT PRIMARY KEY");
    EXPECT_EQ(dialect.renderPagination({}), "");
    EXPECT_EQ(dialect.renderPagination({.limit = 10}), " LIMIT 10");
    EXPECT_EQ(dialect.renderPagination({.offset = 3}), " LIMIT 18446744073709551615 OFFSET 3");
    EXPECT_EQ(dialect.renderPagination({.limit = 10, .offset = 3}), " LIMIT 10 OFFSET 3");
    EXPECT_EQ(dialect.renderAggregateResult("SUM(`value`)", true), "CAST(SUM(`value`) AS CHAR)");
    EXPECT_EQ(dialect.renderAggregateResult("COUNT(*)", false), "COUNT(*)");
    EXPECT_EQ(dialect.renderInsertIfAbsent(
                  {.tableName = "links", .columns = {"id"}, .valueExpressions = {":id"}, .conflictColumns = {"id"}}),
              "INSERT INTO `links` (`id`) VALUES (:id);");
    EXPECT_THROW((void)dialect.renderInsertIfAbsent({}), std::invalid_argument);
    EXPECT_THROW(
        (void)dialect.renderInsertIfAbsent(
            {.tableName = "links", .columns = {"id"}, .valueExpressions = {":id"}, .conflictColumns = {"other"}}),
        std::invalid_argument);
}
