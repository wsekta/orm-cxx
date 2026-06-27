#include "orm-cxx/database.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <vector>

#include "soci/values.h"

namespace
{
auto nullParameter(orm::model::ColumnType type) -> orm::db::StatementParameter
{
    return orm::db::StatementParameter{.name = "value", .value = std::nullopt, .nullType = type};
}
} // namespace

TEST(StatementParameterBindingTest, shouldBindSupportedNullParameterTypes)
{
    const auto supportedTypes = std::vector<orm::model::ColumnType>{
        orm::model::ColumnType::Bool,
        orm::model::ColumnType::Char,
        orm::model::ColumnType::UnsignedChar,
        orm::model::ColumnType::Short,
        orm::model::ColumnType::UnsignedShort,
        orm::model::ColumnType::Int,
        orm::model::ColumnType::UnsignedInt,
        orm::model::ColumnType::UnsignedLongLong,
        orm::model::ColumnType::LongLong,
        orm::model::ColumnType::Float,
        orm::model::ColumnType::Double,
        orm::model::ColumnType::String,
    };

    for (const auto type : supportedTypes)
    {
        soci::values values;

        EXPECT_NO_THROW(orm::detail::bindStatementParameter(values, nullParameter(type)));
    }
}

TEST(StatementParameterBindingTest, shouldRejectUnsupportedNullParameterTypes)
{
    const auto unsupportedTypes = std::vector<orm::model::ColumnType>{
        orm::model::ColumnType::Uuid,
        orm::model::ColumnType::Unknown,
        orm::model::ColumnType::OneToOne,
        static_cast<orm::model::ColumnType>(999),
    };

    for (const auto type : unsupportedTypes)
    {
        soci::values values;

        EXPECT_THROW(orm::detail::bindStatementParameter(values, nullParameter(type)), std::invalid_argument);
    }
}

TEST(StatementParameterBindingTest, shouldBindPresentParameterValue)
{
    soci::values values;
    const auto parameter = orm::db::StatementParameter{.name = "value", .value = orm::query::QueryValue{42}};

    EXPECT_NO_THROW(orm::detail::bindStatementParameter(values, parameter));
}

TEST(StatementParameterBindingTest, shouldNormalizeReportedAffectedRows)
{
    EXPECT_EQ(orm::detail::normalizeAffectedRows(3), 3);
}

TEST(StatementParameterBindingTest, shouldRejectNegativeAffectedRows)
{
    EXPECT_THROW((void)orm::detail::normalizeAffectedRows(-1), std::runtime_error);
}
