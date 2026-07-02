#include "orm-cxx/projection_query.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>

#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/FakeDatabase.hpp"

using namespace orm::query;

namespace projection_query_test_models
{
struct ProjectionDto
{
    int id;
    std::string name;
};

struct ProjectionDtoWithOptional
{
    int id;
    std::optional<std::string> name;
};

struct ProjectionDtoWithUnsupportedField
{
    int id;
    models::ModelWithId nested;
};
} // namespace projection_query_test_models

using namespace projection_query_test_models;

TEST(ProjectionQueryTest, shouldCreateProjectionAlias)
{
    const auto projection = as("name", col("displayName"));

    EXPECT_EQ(projection.resultField, "name");
    EXPECT_EQ(projection.sourceColumn.getPath(), "displayName");
}

TEST(ProjectionQueryTest, shouldIdentifySupportedProjectionResultTypes)
{
    const auto supportedTypes = std::vector<orm::model::ColumnType>{
        orm::model::ColumnType::Bool,
        orm::model::ColumnType::Char,
        orm::model::ColumnType::UnsignedChar,
        orm::model::ColumnType::Short,
        orm::model::ColumnType::UnsignedShort,
        orm::model::ColumnType::Int,
        orm::model::ColumnType::UnsignedInt,
        orm::model::ColumnType::LongLong,
        orm::model::ColumnType::UnsignedLongLong,
        orm::model::ColumnType::Float,
        orm::model::ColumnType::Double,
        orm::model::ColumnType::String,
    };

    for (const auto type : supportedTypes)
    {
        EXPECT_TRUE(orm::detail::isSupportedProjectionResultType(type));
    }

    const auto unsupportedTypes = std::vector<orm::model::ColumnType>{
        orm::model::ColumnType::Uuid,
        orm::model::ColumnType::Unknown,
        orm::model::ColumnType::OneToOne,
        static_cast<orm::model::ColumnType>(999),
    };

    for (const auto type : unsupportedTypes)
    {
        EXPECT_FALSE(orm::detail::isSupportedProjectionResultType(type));
    }
}

TEST(ProjectionQueryTest, shouldStoreProjectionDataAndSupportChaining)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDto> query;

    query.project(as("id", col("id")), as("name", col("field2")))
        .where(col("id") == 1)
        .orderBy(desc(col("id")))
        .distinct()
        .limit(10)
        .offset(5)
        .disableJoining();

    const auto& data = orm::Database::getQueryData(query);

    ASSERT_EQ(data.projections.size(), 2);
    EXPECT_EQ(data.projections[0].resultField, "id");
    EXPECT_EQ(data.projections[0].sourceColumn.getPath(), "id");
    EXPECT_EQ(data.projections[1].resultField, "name");
    EXPECT_EQ(data.projections[1].sourceColumn.getPath(), "field2");
    EXPECT_TRUE(data.predicate.has_value());
    ASSERT_EQ(data.orderBy.size(), 1);
    EXPECT_TRUE(data.isDistinct);
    ASSERT_TRUE(data.limit.has_value());
    EXPECT_EQ(data.limit.value(), 10);
    ASSERT_TRUE(data.offset.has_value());
    EXPECT_EQ(data.offset.value(), 5);
    EXPECT_FALSE(data.shouldJoin);
}

TEST(ProjectionQueryTest, shouldAllowOptionalResultFields)
{
    orm::ProjectionQuery<models::ModelWithOptional, ProjectionDtoWithOptional> query;

    EXPECT_NO_THROW(query.project(as("id", col("field1")), as("name", col("field2"))));
}

TEST(ProjectionQueryTest, shouldRejectEmptyProjectionList)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDto> query;

    EXPECT_THROW(query.project(), std::invalid_argument);
}

TEST(ProjectionQueryTest, shouldRejectEmptyProjectionAlias)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDto> query;

    EXPECT_THROW(query.project(as("", col("id")), as("name", col("field2"))), std::invalid_argument);
}

TEST(ProjectionQueryTest, shouldRejectDuplicateProjectionAlias)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDto> query;

    EXPECT_THROW(query.project(as("id", col("id")), as("id", col("field2"))), std::invalid_argument);
}

TEST(ProjectionQueryTest, shouldRejectAliasThatDoesNotMatchDtoField)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDto> query;

    EXPECT_THROW(query.project(as("id", col("id")), as("missing", col("field2"))), std::invalid_argument);
}

TEST(ProjectionQueryTest, shouldRejectMissingDtoFieldAlias)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDto> query;

    EXPECT_THROW(query.project(as("id", col("id"))), std::invalid_argument);
}

TEST(ProjectionQueryTest, shouldRejectUnsupportedDtoField)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDtoWithUnsupportedField> query;

    EXPECT_THROW(query.project(as("id", col("id")), as("nested", col("field1"))), std::invalid_argument);
}
