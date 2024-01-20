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
