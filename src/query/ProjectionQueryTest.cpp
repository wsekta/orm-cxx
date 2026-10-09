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

struct AggregateProjectionDto
{
    std::string name;
    long long users;
    double averageField1;
};
} // namespace projection_query_test_models

using namespace projection_query_test_models;

namespace
{
auto projectionColumnPath(const detail::Projection& projection) -> std::string
{
    return std::get<detail::Column>(projection.source).getPath();
}
} // namespace

TEST(ProjectionQueryTest, shouldCreateProjectionAlias)
{
    const auto projection = detail::erase(as("name", col<&models::ModelWithId::field2>()));

    EXPECT_EQ(projection.resultField, "name");
    EXPECT_EQ(projectionColumnPath(projection), "field2");
}

TEST(ProjectionQueryTest, shouldCreateAggregateProjectionAlias)
{
    const auto projection = detail::erase(as("users", countAll<models::ModelWithId>()));
    const auto& aggregate = std::get<detail::AggregateExpression>(projection.source);

    EXPECT_EQ(projection.resultField, "users");
    EXPECT_EQ(aggregate.function, detail::AggregateFunction::CountAll);
    EXPECT_FALSE(aggregate.column.has_value());
}

TEST(ProjectionQueryTest, shouldCreateAggregateExpressions)
{
    const auto aggregates = std::vector<std::pair<detail::AggregateExpression, detail::AggregateFunction>>{
        {detail::erase(count(col<&models::ModelWithId::field1>())), detail::AggregateFunction::Count},
        {detail::erase(sum(col<&models::ModelWithId::field1>())), detail::AggregateFunction::Sum},
        {detail::erase(avg(col<&models::ModelWithId::field1>())), detail::AggregateFunction::Avg},
        {detail::erase(min(col<&models::ModelWithId::field1>())), detail::AggregateFunction::Min},
        {detail::erase(max(col<&models::ModelWithId::field1>())), detail::AggregateFunction::Max},
    };

    for (const auto& [aggregate, function] : aggregates)
    {
        EXPECT_EQ(aggregate.function, function);
        ASSERT_TRUE(aggregate.column.has_value());
        EXPECT_EQ(aggregate.column->getPath(), "field1");
    }
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
        static_cast<orm::model::ColumnType>(999), // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    };

    for (const auto type : unsupportedTypes)
    {
        EXPECT_FALSE(orm::detail::isSupportedProjectionResultType(type));
    }
}

TEST(ProjectionQueryTest, shouldStoreProjectionDataAndSupportChaining)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDto> query;

    query.project(as("id", col<&models::ModelWithId::id>()), as("name", col<&models::ModelWithId::field2>()))
        .where(col<&models::ModelWithId::id>() == 1)
        .orderBy(desc(col<&models::ModelWithId::id>()))
        .distinct()
        .limit(10)
        .offset(5)
        .disableJoining();

    const auto& data = orm::FakeDatabase::getSelectSpec(query);

    ASSERT_EQ(data.projections.size(), 2);
    EXPECT_EQ(data.projections[0].resultField, "id");
    EXPECT_EQ(projectionColumnPath(data.projections[0]), "id");
    EXPECT_EQ(data.projections[1].resultField, "name");
    EXPECT_EQ(projectionColumnPath(data.projections[1]), "field2");
    EXPECT_TRUE(data.predicate.has_value());
    ASSERT_EQ(data.orderBy.size(), 1);
    EXPECT_TRUE(data.isDistinct);
    ASSERT_TRUE(data.limit.has_value());
    EXPECT_EQ(data.limit.value(), 10);
    ASSERT_TRUE(data.offset.has_value());
    EXPECT_EQ(data.offset.value(), 5);
    EXPECT_FALSE(data.shouldJoin);
}

TEST(ProjectionQueryTest, shouldStoreGroupByAndHavingData)
{
    orm::ProjectionQuery<models::ModelWithId, AggregateProjectionDto> query;

    query
        .project(as("name", col<&models::ModelWithId::field2>()), as("users", countAll<models::ModelWithId>()),
                 as("averageField1", avg(col<&models::ModelWithId::field1>())))
        .groupBy(col<&models::ModelWithId::field2>())
        .having(countAll<models::ModelWithId>() > 1)
        .andHaving(avg(col<&models::ModelWithId::field1>()) >= 10.0)
        .orHaving(max(col<&models::ModelWithId::id>()) == 3);

    const auto& data = orm::FakeDatabase::getSelectSpec(query);

    ASSERT_EQ(data.groupBy.size(), 1);
    EXPECT_EQ(data.groupBy[0].getPath(), "field2");
    ASSERT_TRUE(data.having.has_value());

    const auto& havingRoot = data.having->getNode();
    ASSERT_TRUE(std::holds_alternative<detail::AggregateLogicalExpression>(havingRoot.expression));
    EXPECT_EQ(std::get<detail::AggregateLogicalExpression>(havingRoot.expression).logicalOperator,
              detail::LogicalOperator::Or);
}

TEST(ProjectionQueryTest, shouldAllowOptionalResultFields)
{
    orm::ProjectionQuery<models::ModelWithOptional, ProjectionDtoWithOptional> query;

    EXPECT_NO_THROW(query.project(as("id", col<&models::ModelWithOptional::field1>()),
                                  as("name", col<&models::ModelWithOptional::field2>())));
}

TEST(ProjectionQueryTest, shouldRejectEmptyProjectionList)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDto> query;

    EXPECT_THROW(query.project(), std::invalid_argument);
}

TEST(ProjectionQueryTest, shouldRejectEmptyProjectionAlias)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDto> query;

    EXPECT_THROW(
        query.project(as("", col<&models::ModelWithId::id>()), as("name", col<&models::ModelWithId::field2>())),
        std::invalid_argument);
}

TEST(ProjectionQueryTest, shouldRejectDuplicateProjectionAlias)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDto> query;

    EXPECT_THROW(
        query.project(as("id", col<&models::ModelWithId::id>()), as("id", col<&models::ModelWithId::field2>())),
        std::invalid_argument);
}

TEST(ProjectionQueryTest, shouldRejectAliasThatDoesNotMatchDtoField)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDto> query;

    EXPECT_THROW(
        query.project(as("id", col<&models::ModelWithId::id>()), as("missing", col<&models::ModelWithId::field2>())),
        std::invalid_argument);
}

TEST(ProjectionQueryTest, shouldRejectMissingDtoFieldAlias)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDto> query;

    EXPECT_THROW(query.project(as("id", col<&models::ModelWithId::id>())), std::invalid_argument);
}

TEST(ProjectionQueryTest, shouldRejectUnsupportedDtoField)
{
    orm::ProjectionQuery<models::ModelWithId, ProjectionDtoWithUnsupportedField> query;

    EXPECT_THROW(
        query.project(as("id", col<&models::ModelWithId::id>()), as("nested", col<&models::ModelWithId::field1>())),
        std::invalid_argument);
}
