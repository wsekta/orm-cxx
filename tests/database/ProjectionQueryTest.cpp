#include "DatabaseTest.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

struct UnsupportedProjection
{
    int id;
    models::ModelWithId nested;
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

    EXPECT_THROW(soci::type_conversion<Payload>::from_base(values, soci::i_ok, payload), std::invalid_argument);
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

INSTANTIATE_TEST_SUITE_P(DatabaseTest, ProjectionQueryDatabaseTest, connectionStrings);
