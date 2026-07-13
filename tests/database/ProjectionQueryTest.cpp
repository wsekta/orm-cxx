#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "DatabaseTest.hpp"
#include "orm-cxx/database/binding/ProjectionBinding.hpp"
#include "soci/values.h"

using namespace orm::query;

namespace projection_query_database_test_models
{
struct ModelWithIdProjection
{
    int id;
    std::string name;
};

struct NumericProjection
{
    float floatValue;
    std::uint64_t unsignedValue;
};

struct WideIntegralProjection
{
    long long signedValue;
    unsigned long long unsignedValue;
};

struct NumericFromStringProjection
{
    int intValue;
    unsigned long long unsignedValue;
    double doubleValue;
};

struct OptionalProjection
{
    int id;
    std::optional<std::string> name;
};

struct RelatedProjection
{
    int id;
    std::string relatedName;
};

struct AggregateSummaryProjection
{
    std::string name;
    long long users;
    long long totalField1;
    double averageField1;
    int minField1;
    int maxField1;
};

struct RelatedAggregateSummaryProjection
{
    std::string relatedName;
    long long users;
};

struct NullableAggregateProjection
{
    std::optional<double> averageValue;
};

struct UnsupportedProjection
{
    int id;
    models::ModelWithId nested;
};

struct InvalidNumericProjection
{
    int value;
};

struct NarrowIntegralProjection
{
    unsigned char value;
};
} // namespace projection_query_database_test_models

using namespace projection_query_database_test_models;

class ProjectionQueryDatabaseTest : public DatabaseTest
{
};

TEST(ProjectionBindingTest, shouldHydrateProjectionDto)
{
    using Payload = orm::db::binding::ProjectionPayload<ModelWithIdProjection>;

    auto payload = Payload{};
    auto values = soci::values{};

    values.set("id", 7);
    values.set("name", std::string{"projected"});

    soci::type_conversion<Payload>::from_base(values, soci::i_ok, payload);

    EXPECT_EQ(payload.value.id, 7);
    EXPECT_EQ(payload.value.name, "projected");
}

TEST(ProjectionBindingTest, shouldHydrateConvertedProjectionFields)
{
    using Payload = orm::db::binding::ProjectionPayload<NumericProjection>;

    auto payload = Payload{};
    auto values = soci::values{};

    values.set("floatValue", 2.5);
    values.set("unsignedValue", static_cast<unsigned long long>(42));

    soci::type_conversion<Payload>::from_base(values, soci::i_ok, payload);

    EXPECT_FLOAT_EQ(payload.value.floatValue, 2.5F);
    EXPECT_EQ(payload.value.unsignedValue, 42);
}

TEST(ProjectionBindingTest, shouldHydrateWideIntegralProjectionFieldsFromIntValues)
{
    using Payload = orm::db::binding::ProjectionPayload<WideIntegralProjection>;

    auto payload = Payload{};
    auto values = soci::values{};

    values.set("signedValue", 7);
    values.set("unsignedValue", 9);

    soci::type_conversion<Payload>::from_base(values, soci::i_ok, payload);

    EXPECT_EQ(payload.value.signedValue, 7);
    EXPECT_EQ(payload.value.unsignedValue, 9);
}

TEST(ProjectionBindingTest, shouldHydrateNumericProjectionFieldsFromStringValues)
{
    using Payload = orm::db::binding::ProjectionPayload<NumericFromStringProjection>;

    auto payload = Payload{};
    auto values = soci::values{};

    values.set("intValue", std::string{"12"});
    values.set("unsignedValue", std::string{"42"});
    values.set("doubleValue", std::string{"2.5"});

    soci::type_conversion<Payload>::from_base(values, soci::i_ok, payload);

    EXPECT_EQ(payload.value.intValue, 12);
    EXPECT_EQ(payload.value.unsignedValue, 42);
    EXPECT_DOUBLE_EQ(payload.value.doubleValue, 2.5);
}

TEST(ProjectionBindingTest, shouldRejectInvalidNumericProjectionFieldValue)
{
    using Payload = orm::db::binding::ProjectionPayload<InvalidNumericProjection>;

    auto payload = Payload{};
    auto values = soci::values{};

    values.set("value", std::string{"not-a-number"});

    EXPECT_THROW(soci::type_conversion<Payload>::from_base(values, soci::i_ok, payload),
                 orm::db::binding::ConversionError);
}

TEST(ProjectionBindingTest, shouldRejectWhitespacePrefixedNegativeUnsignedProjectionValue)
{
    using Payload = orm::db::binding::ProjectionPayload<NumericFromStringProjection>;

    auto payload = Payload{};
    auto values = soci::values{};

    values.set("intValue", std::string{"12"});
    values.set("unsignedValue", std::string{" -1"});
    values.set("doubleValue", std::string{"2.5"});

    EXPECT_THROW(soci::type_conversion<Payload>::from_base(values, soci::i_ok, payload),
                 orm::db::binding::ConversionError);
}

TEST(ProjectionBindingTest, shouldRejectLossyNumericProjectionConversion)
{
    using Payload = orm::db::binding::ProjectionPayload<NarrowIntegralProjection>;

    auto payload = Payload{};
    auto values = soci::values{};
    values.set("value", 300);

    EXPECT_THROW(soci::type_conversion<Payload>::from_base(values, soci::i_ok, payload),
                 orm::db::binding::ConversionError);
}

TEST(ProjectionBindingTest, shouldHydrateOptionalProjectionFields)
{
    using Payload = orm::db::binding::ProjectionPayload<OptionalProjection>;

    auto presentPayload = Payload{};
    auto presentValues = soci::values{};

    presentValues.set("id", 1);
    presentValues.set("name", std::string{"present"});

    soci::type_conversion<Payload>::from_base(presentValues, soci::i_ok, presentPayload);

    EXPECT_EQ(presentPayload.value.id, 1);
    ASSERT_TRUE(presentPayload.value.name.has_value());
    EXPECT_EQ(presentPayload.value.name.value(), "present");

    auto nullPayload = Payload{};
    auto nullValues = soci::values{};

    nullValues.set("id", 2);
    nullValues.set("name", std::string{});
    nullValues.set("name", std::string{}, soci::i_null);

    soci::type_conversion<Payload>::from_base(nullValues, soci::i_ok, nullPayload);

    EXPECT_EQ(nullPayload.value.id, 2);
    EXPECT_FALSE(nullPayload.value.name.has_value());
}

TEST(ProjectionBindingTest, shouldRejectUnsupportedProjectionField)
{
    using Payload = orm::db::binding::ProjectionPayload<UnsupportedProjection>;

    auto payload = Payload{};
    auto values = soci::values{};

    values.set("id", 1);

    EXPECT_THROW(soci::type_conversion<Payload>::from_base(values, soci::i_ok, payload),
                 orm::db::binding::ConversionError);
}

TEST_P(ProjectionQueryDatabaseTest, shouldReportLossyHydrationAsStructuredConversionError)
{
    createTable<models::ModelWithId>();
    database.insert(models::ModelWithId{1, 300, "too-wide"});

    orm::ProjectionQuery<models::ModelWithId, NarrowIntegralProjection> query;
    query.project(as("value", col("field1")));

    try
    {
        (void)database.select(query);
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Conversion);
        EXPECT_EQ(error.getBackendType(), GetParam().type);
        EXPECT_EQ(error.getOperation(), "select projection");
    }
}

TEST_P(ProjectionQueryDatabaseTest, shouldSelectProjectedDtos)
{
    createTable<models::ModelWithId>();
    database.insert(std::vector<models::ModelWithId>{{1, 10, "first"}, {2, 20, "second"}, {3, 30, "third"}});

    orm::ProjectionQuery<models::ModelWithId, ModelWithIdProjection> query;
    query.project(as("id", col("id")), as("name", col("field2")))
        .where(col("id") >= 2)
        .orderBy(desc(col("id")))
        .limit(1);

    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].id, 3);
    EXPECT_EQ(rows[0].name, "third");
}

TEST_P(ProjectionQueryDatabaseTest, shouldSelectProjectedRelatedField)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();
    const auto relatedModels = std::vector<models::ModelWithId>{{1, 10, "profile-one"}, {2, 20, "profile-two"}};
    const auto models = std::vector<models::ModelRelatedToOtherModel>{{1, 100, "first", relatedModels[0]},
                                                                      {2, 200, "second", relatedModels[1]}};

    database.insert(relatedModels);
    database.insert(models);

    orm::ProjectionQuery<models::ModelRelatedToOtherModel, RelatedProjection> query;
    query.project(as("id", col("id")), as("relatedName", col("field3.field2"))).where(col("id") == 2);

    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].id, 2);
    EXPECT_EQ(rows[0].relatedName, "profile-two");
}

TEST_P(ProjectionQueryDatabaseTest, shouldSelectProjectedOptionalField)
{
    createTable<models::ModelWithOptional>();
    database.insert(std::vector<models::ModelWithOptional>{{1, std::nullopt, 1.0}, {2, "present", 2.0}});

    orm::ProjectionQuery<models::ModelWithOptional, OptionalProjection> query;
    query.project(as("id", col("field1")), as("name", col("field2"))).orderBy(asc(col("field1")));

    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].id, 1);
    EXPECT_FALSE(rows[0].name.has_value());
    EXPECT_EQ(rows[1].id, 2);
    ASSERT_TRUE(rows[1].name.has_value());
    EXPECT_EQ(rows[1].name.value(), "present");
}

TEST_P(ProjectionQueryDatabaseTest, shouldSelectAggregateProjectionGroupedByScalarField)
{
    createTable<models::ModelWithId>();
    database.insert(std::vector<models::ModelWithId>{
        {1, 10, "alpha"}, {2, 20, "alpha"}, {3, 30, "beta"}, {4, 40, "beta"}, {5, 50, "beta"}});

    orm::ProjectionQuery<models::ModelWithId, AggregateSummaryProjection> query;
    query
        .project(as("name", col("field2")), as("users", countAll()), as("totalField1", sum(col("field1"))),
                 as("averageField1", avg(col("field1"))), as("minField1", min(col("field1"))),
                 as("maxField1", max(col("field1"))))
        .groupBy(col("field2"))
        .having(countAll() > 2)
        .orderBy(asc(col("field2")));

    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].name, "beta");
    EXPECT_EQ(rows[0].users, 3);
    EXPECT_EQ(rows[0].totalField1, 120);
    EXPECT_DOUBLE_EQ(rows[0].averageField1, 40.0);
    EXPECT_EQ(rows[0].minField1, 30);
    EXPECT_EQ(rows[0].maxField1, 50);
}

TEST_P(ProjectionQueryDatabaseTest, shouldSelectAggregateProjectionGroupedByRelatedField)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();
    const auto relatedModels = std::vector<models::ModelWithId>{{1, 10, "profile-one"}, {2, 20, "profile-two"}};
    const auto models = std::vector<models::ModelRelatedToOtherModel>{
        {1, 100, "first", relatedModels[0]}, {2, 200, "second", relatedModels[0]}, {3, 300, "third", relatedModels[1]}};

    database.insert(relatedModels);
    database.insert(models);

    orm::ProjectionQuery<models::ModelRelatedToOtherModel, RelatedAggregateSummaryProjection> query;
    query.project(as("relatedName", col("field3.field2")), as("users", countAll()))
        .groupBy(col("field3.field2"))
        .orderBy(asc(col("field3.field2")));

    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].relatedName, "profile-one");
    EXPECT_EQ(rows[0].users, 2);
    EXPECT_EQ(rows[1].relatedName, "profile-two");
    EXPECT_EQ(rows[1].users, 1);
}

TEST_P(ProjectionQueryDatabaseTest, shouldSelectNullableAggregateProjection)
{
    createTable<models::ModelWithOptional>();
    database.insert(std::vector<models::ModelWithOptional>{{1, "present", 1.0}, {2, "present", 2.0}});

    orm::ProjectionQuery<models::ModelWithOptional, NullableAggregateProjection> query;
    query.project(as("averageValue", avg(col("field3")))).where(col("field1") == 999);

    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_FALSE(rows[0].averageValue.has_value());
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, ProjectionQueryDatabaseTest, backendTestConfigs, backendTestName);
