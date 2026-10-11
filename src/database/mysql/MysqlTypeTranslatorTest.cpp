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

TEST(MysqlTypeTranslatorTest, preservesAllScalarRangesAndRequiresBoundedVarchar)
{
    const orm::db::mysql::MysqlTypeTranslator translator;
    using enum orm::model::ColumnType;
    const std::array<std::pair<orm::model::ColumnType, std::string_view>, 12> expected{
        {{Bool, "TINYINT"},
         {Char, "SMALLINT"},
         {UnsignedChar, "TINYINT UNSIGNED"},
         {Short, "SMALLINT"},
         {UnsignedShort, "SMALLINT UNSIGNED"},
         {Int, "INT"},
         {UnsignedInt, "INT UNSIGNED"},
         {LongLong, "BIGINT"},
         {UnsignedLongLong, "BIGINT UNSIGNED"},
         {Float, "DOUBLE"},
         {Double, "DOUBLE"},
         {String, "LONGTEXT"}}};
    for (const auto& [type, sql] : expected)
        EXPECT_EQ(translator.toSqlType(type), sql);
    EXPECT_EQ(translator.toSqlType(String, 255), "VARCHAR(255)");
    EXPECT_EQ(translator.toSqlType(String, 16383), "VARCHAR(16383)");
    EXPECT_THROW((void)translator.toSqlType(String, 0), std::invalid_argument);
    EXPECT_THROW((void)translator.toSqlType(String, 16384), std::invalid_argument);
    EXPECT_THROW((void)translator.toSqlType(Uuid), std::runtime_error);
}
