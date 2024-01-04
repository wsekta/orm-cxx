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
    EXPECT_EQ(translator.toSqlType("int"), "INTEGER");
}

TEST_F(SqliteTypeTranslatorTest, shouldTranslateString)
{
    EXPECT_EQ(translator.toSqlType("std::basic_string<char>"), "TEXT");
}