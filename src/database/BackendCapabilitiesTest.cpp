#include "orm-cxx/database/BackendCapabilities.hpp"

#include <gtest/gtest.h>

TEST(BackendCapabilitiesTest, defaultsToNoOptionalBackendFeatures)
{
    const orm::db::BackendCapabilities capabilities;

    EXPECT_FALSE(capabilities.schema.createTableIfNotExists);
    EXPECT_FALSE(capabilities.query.limit);
    EXPECT_FALSE(capabilities.query.projections);
    EXPECT_FALSE(capabilities.query.groupBy);
    EXPECT_FALSE(capabilities.query.having);
    EXPECT_FALSE(capabilities.query.collectionPredicates);
    EXPECT_FALSE(capabilities.mutations.insert);
    EXPECT_FALSE(capabilities.mutations.update);
    EXPECT_FALSE(capabilities.mutations.remove);
    EXPECT_FALSE(capabilities.mutations.atomicInsertIfAbsent);
    EXPECT_EQ(capabilities.mutations.affectedRows, orm::db::AffectedRowsSupport::Unavailable);
    EXPECT_FALSE(capabilities.relations.junctionTables);
    EXPECT_FALSE(capabilities.relations.toOne);
    EXPECT_FALSE(capabilities.relations.oneToMany);
    EXPECT_FALSE(capabilities.relations.manyToMany);
    EXPECT_FALSE(capabilities.relations.collectionIncludes);
    EXPECT_FALSE(capabilities.relations.collectionPredicates);
    EXPECT_FALSE(capabilities.transactions);
    EXPECT_FALSE(capabilities.supportsColumnType(orm::model::ColumnType::Int));
    EXPECT_FALSE(capabilities.valueLimits.maxUnsignedLongLong.has_value());
}

TEST(BackendCapabilitiesTest, reportsExplicitlySupportedColumnTypes)
{
    const orm::db::BackendCapabilities capabilities{
        .supportedColumnTypes = {orm::model::ColumnType::Int, orm::model::ColumnType::String}};

    EXPECT_TRUE(capabilities.supportsColumnType(orm::model::ColumnType::Int));
    EXPECT_TRUE(capabilities.supportsColumnType(orm::model::ColumnType::String));
    EXPECT_FALSE(capabilities.supportsColumnType(orm::model::ColumnType::Uuid));
}

TEST(BackendCapabilitiesTest, runtimeBindParameterLimitCanBeUnknown)
{
    const orm::db::BackendRuntimeLimits unknown;
    const orm::db::BackendRuntimeLimits bounded{.maxBindParameters = 900};

    EXPECT_FALSE(unknown.maxBindParameters.has_value());
    ASSERT_TRUE(bounded.maxBindParameters.has_value());
    EXPECT_EQ(bounded.maxBindParameters.value(), 900);
}

TEST(BackendCapabilitiesTest, valueRangeCanDeclareALosslessUnsigned64BitMaximum)
{
    const orm::db::BackendValueLimits unlimited;
    const orm::db::BackendValueLimits bounded{.maxUnsignedLongLong = 42};

    EXPECT_FALSE(unlimited.maxUnsignedLongLong.has_value());
    EXPECT_EQ(bounded.maxUnsignedLongLong, 42);
}
