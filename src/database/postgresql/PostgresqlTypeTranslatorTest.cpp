#include "PostgresqlTypeTranslator.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

namespace
{
const orm::db::postgresql::PostgresqlTypeTranslator translator;
}

TEST(PostgresqlTypeTranslatorTest, translatesEverySupportedColumnType)
{
    using orm::model::ColumnType;

    EXPECT_EQ(translator.toSqlType(ColumnType::Bool), "BOOLEAN");
    EXPECT_EQ(translator.toSqlType(ColumnType::Char), "SMALLINT");
    EXPECT_EQ(translator.toSqlType(ColumnType::UnsignedChar), "SMALLINT");
    EXPECT_EQ(translator.toSqlType(ColumnType::Short), "SMALLINT");
    EXPECT_EQ(translator.toSqlType(ColumnType::UnsignedShort), "INTEGER");
    EXPECT_EQ(translator.toSqlType(ColumnType::Int), "INTEGER");
    EXPECT_EQ(translator.toSqlType(ColumnType::UnsignedInt), "BIGINT");
    EXPECT_EQ(translator.toSqlType(ColumnType::LongLong), "BIGINT");
    EXPECT_EQ(translator.toSqlType(ColumnType::UnsignedLongLong), "BIGINT");
    EXPECT_EQ(translator.toSqlType(ColumnType::Float), "DOUBLE PRECISION");
    EXPECT_EQ(translator.toSqlType(ColumnType::Double), "DOUBLE PRECISION");
    EXPECT_EQ(translator.toSqlType(ColumnType::String), "TEXT");
}

TEST(PostgresqlTypeTranslatorTest, rejectsUnsupportedColumnTypes)
{
    EXPECT_THROW((void)translator.toSqlType(orm::model::ColumnType::Uuid), std::runtime_error);

    const auto invalidColumnType =
        static_cast<orm::model::ColumnType>(999); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    EXPECT_THROW((void)translator.toSqlType(invalidColumnType), std::runtime_error);
}
