#include "SqliteTypeTranslator.hpp"

#include <gtest/gtest.h>

using namespace orm::db::sqlite;

class SqliteTypeTranslatorTest : public ::testing::Test
{
public:
    SqliteTypeTranslator translator;
};

TEST_F(SqliteTypeTranslatorTest, shouldTranslateInt)
{
    EXPECT_EQ(translator.toSqlType(orm::model::ColumnType::Int), "INTEGER");
}

TEST_F(SqliteTypeTranslatorTest, shouldTranslateFloat)
{
    EXPECT_EQ(translator.toSqlType(orm::model::ColumnType::Float), "REAL");
}

TEST_F(SqliteTypeTranslatorTest, shouldTranslateDouble)
{
    EXPECT_EQ(translator.toSqlType(orm::model::ColumnType::Double), "REAL");
}

TEST_F(SqliteTypeTranslatorTest, shouldTranslateString)
{
    EXPECT_EQ(translator.toSqlType(orm::model::ColumnType::String), "TEXT");
}

TEST_F(SqliteTypeTranslatorTest, shouldTranslateBool)
{
    EXPECT_EQ(translator.toSqlType(orm::model::ColumnType::Bool), "BOOLEAN");
}

TEST_F(SqliteTypeTranslatorTest, shouldTranslateChar)
{
    EXPECT_EQ(translator.toSqlType(orm::model::ColumnType::Char), "TINYINT");
}

TEST_F(SqliteTypeTranslatorTest, shouldTranslateUnsignedChar)
{
    EXPECT_EQ(translator.toSqlType(orm::model::ColumnType::UnsignedChar), "TINYINT");
}

TEST_F(SqliteTypeTranslatorTest, shouldTranslateShort)
{
    EXPECT_EQ(translator.toSqlType(orm::model::ColumnType::Short), "SMALLINT");
}

TEST_F(SqliteTypeTranslatorTest, shouldTranslateUnsignedShort)
{
    EXPECT_EQ(translator.toSqlType(orm::model::ColumnType::UnsignedShort), "SMALLINT");
}

TEST_F(SqliteTypeTranslatorTest, shouldTranslateLongLong)
{
    EXPECT_EQ(translator.toSqlType(orm::model::ColumnType::LongLong), "BIGINT");
}

TEST_F(SqliteTypeTranslatorTest, shouldTranslateUnsignedLongLong)
{
    EXPECT_EQ(translator.toSqlType(orm::model::ColumnType::UnsignedLongLong), "UNSIGNED BIG INT");
}

TEST_F(SqliteTypeTranslatorTest, shouldThrowOnUnsupportedType)
{
    EXPECT_THROW(translator.toSqlType(orm::model::ColumnType::Uuid), std::runtime_error);
}
