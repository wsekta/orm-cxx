module;

#include <typeinfo>

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

struct UserId
{
    orm::Uuid value;
    auto operator==(const UserId&) const -> bool = default;
};

struct OrderId
{
    orm::Uuid value;
};

struct UuidRecord
{
    UserId id;
    std::optional<UserId> optionalId;
    orm::Uuid nativeId;
};

struct UserIdDto
{
    UserId id;
};

struct OptionalUserIdDto
{
    std::optional<UserId> id;
};

struct OrderIdDto
{
    OrderId id;
};

struct NativeUuidDto
{
    orm::Uuid id;
};

struct RepeatedUserIdDto
{
    UserId id;
    std::optional<UserId> copy;
};

struct MixedUuidDto
{
    UserId id;
    OrderId copy;
};
} // namespace projection_query_test_models

template <>
struct orm::UuidAdapter<projection_query_test_models::UserId>
{
    static auto toUuid(const projection_query_test_models::UserId& value) -> orm::Uuid
    {
        return value.value;
    }
    static auto fromUuid(const orm::Uuid& value) -> projection_query_test_models::UserId
    {
        return {value};
    }
};

template <>
struct orm::UuidAdapter<projection_query_test_models::OrderId>
{
    static auto toUuid(const projection_query_test_models::OrderId& value) -> orm::Uuid
    {
        return value.value;
    }
    static auto fromUuid(const orm::Uuid& value) -> projection_query_test_models::OrderId
    {
        return {value};
    }
};

using namespace projection_query_test_models;

namespace
{
template <typename Result, typename... Projections>
concept AcceptsDynamicUuidProjections = requires(orm::ProjectionQuery<UuidRecord, Result> query,
                                                 Projections... projections) { query.project(projections...); };

template <typename Result, typename... Projections>
concept AcceptsStaticUuidProjections =
    requires(Projections... projections) { selectAs<UuidRecord, Result>(projections...); };

using NamedUserProjection = decltype(as<"id">(col<&UuidRecord::id>()));
using NamedOptionalUserProjection = decltype(as<"id">(col<&UuidRecord::optionalId>()));
using NamedNativeProjection = decltype(as<"id">(col<&UuidRecord::nativeId>()));
using NamedCopyProjection = decltype(as<"copy">(col<&UuidRecord::id>()));

// These constraints are checked by immediate functions, not executable line coverage.
// Assert each API separately so one rejecting correctly cannot hide a regression in the other.
static_assert(AcceptsDynamicUuidProjections<UserIdDto, NamedUserProjection>);
static_assert(AcceptsDynamicUuidProjections<OptionalUserIdDto, NamedUserProjection>);
static_assert(AcceptsDynamicUuidProjections<UserIdDto, NamedOptionalUserProjection>);
static_assert(AcceptsDynamicUuidProjections<OptionalUserIdDto, NamedOptionalUserProjection>);
static_assert(AcceptsDynamicUuidProjections<NativeUuidDto, NamedNativeProjection>);
static_assert(!AcceptsDynamicUuidProjections<OrderIdDto, NamedUserProjection>);
static_assert(!AcceptsDynamicUuidProjections<OrderIdDto, NamedOptionalUserProjection>);
static_assert(!AcceptsDynamicUuidProjections<NativeUuidDto, NamedUserProjection>);
static_assert(!AcceptsDynamicUuidProjections<UserIdDto, NamedNativeProjection>);
static_assert(AcceptsDynamicUuidProjections<RepeatedUserIdDto, NamedUserProjection, NamedCopyProjection>);
static_assert(!AcceptsDynamicUuidProjections<MixedUuidDto, NamedUserProjection, NamedCopyProjection>);

static_assert(AcceptsStaticUuidProjections<UserIdDto, NamedUserProjection>);
static_assert(AcceptsStaticUuidProjections<OptionalUserIdDto, NamedUserProjection>);
static_assert(AcceptsStaticUuidProjections<UserIdDto, NamedOptionalUserProjection>);
static_assert(AcceptsStaticUuidProjections<OptionalUserIdDto, NamedOptionalUserProjection>);
static_assert(AcceptsStaticUuidProjections<NativeUuidDto, NamedNativeProjection>);
static_assert(!AcceptsStaticUuidProjections<OrderIdDto, NamedUserProjection>);
static_assert(!AcceptsStaticUuidProjections<OrderIdDto, NamedOptionalUserProjection>);
static_assert(!AcceptsStaticUuidProjections<NativeUuidDto, NamedUserProjection>);
static_assert(!AcceptsStaticUuidProjections<UserIdDto, NamedNativeProjection>);
static_assert(AcceptsStaticUuidProjections<RepeatedUserIdDto, NamedUserProjection, NamedCopyProjection>);
static_assert(!AcceptsStaticUuidProjections<MixedUuidDto, NamedUserProjection, NamedCopyProjection>);

// The values reader is a template seam: fail the string extraction itself,
// without replacing global operator new or throwing later from a UUID adapter.
class ProjectionReadAllocationFailure final : public std::bad_alloc
{
};

template <typename Failure>
struct FailingProjectionValues
{
    mutable std::size_t reads{};
    mutable const std::string* requestedField{};

    template <typename T>
    auto get(const std::string& name) const -> T
    {
        static_assert(std::same_as<T, std::string>);
        ++reads;
        requestedField = &name;
        throw Failure{};
    }

    auto get_indicator(const std::string&) const -> soci::indicator
    {
        return soci::i_ok;
    }
};

template <typename Result>
auto expectUuidDomainRejection(const auto& projection) -> void
{
    orm::ProjectionQuery<UuidRecord, Result> query;
    EXPECT_THAT([&] { query.project(projection); }, ::testing::ThrowsMessage<std::invalid_argument>(
                                                        "Projection UUID domain type does not match the result field"));
}

template <typename Result>
auto expectMatchingUuidDomain(const auto& projection) -> void
{
    orm::ProjectionQuery<UuidRecord, Result> query;
    ASSERT_NO_THROW(query.project(projection));
    const auto& spec = orm::detail::QueryTestAccess::getSelectSpec(query);
    ASSERT_EQ(spec.projections.size(), 1U);
    EXPECT_EQ(spec.projections.front().sourceType, orm::model::typeId<UserId>());
    EXPECT_TRUE(spec.projections.front().sourceUuidAdapted);
}

template <typename Field>
auto expectProjectionReadAllocationFailure(Field initial) -> void
{
    const FailingProjectionValues<ProjectionReadAllocationFailure> values;
    const std::string alias{"id"};
    auto field = initial;

    EXPECT_THROW(orm::db::binding::ObjectFieldFromProjectionValues<Field>::get(&field, alias, values),
                 ProjectionReadAllocationFailure);
    EXPECT_EQ(field, initial);
    EXPECT_EQ(values.reads, 1U);
    EXPECT_EQ(values.requestedField, &alias);
}

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
        orm::model::ColumnType::Uuid,
        orm::model::ColumnType::Date,
        orm::model::ColumnType::TimeOfDay,
        orm::model::ColumnType::LocalDateTime,
        orm::model::ColumnType::UtcTimestamp,
    };

    for (const auto type : supportedTypes)
    {
        EXPECT_TRUE(orm::detail::isSupportedProjectionResultType(type));
    }

    const auto unsupportedTypes = std::vector<orm::model::ColumnType>{
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

    const auto& data = orm::detail::QueryTestAccess::getSelectSpec(query);

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

    const auto& data = orm::detail::QueryTestAccess::getSelectSpec(query);

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

TEST(ProjectionQueryTest, runtimeAliasRejectsDifferentUuidDomains)
{
    const auto projection = as("id", col<&UuidRecord::id>());
    expectUuidDomainRejection<OrderIdDto>(projection);
    expectUuidDomainRejection<OrderIdDto>(projection.dynamic());
    expectUuidDomainRejection<OrderIdDto>(as("id", col<&UuidRecord::optionalId>()));
}

TEST(ProjectionQueryTest, runtimeAliasRejectsAdaptedUuidAsNative)
{
    expectUuidDomainRejection<NativeUuidDto>(as("id", col<&UuidRecord::id>()));
}

TEST(ProjectionQueryTest, runtimeAliasRejectsNativeUuidAsAdapted)
{
    expectUuidDomainRejection<UserIdDto>(as("id", col<&UuidRecord::nativeId>()));
}

TEST(ProjectionQueryTest, erasedNamedProjectionRetainsItsUuidDomain)
{
    const auto user = as<"id">(col<&UuidRecord::id>()).dynamic();
    const auto native = as<"id">(col<&UuidRecord::nativeId>()).dynamic();
    expectUuidDomainRejection<OrderIdDto>(user);
    expectUuidDomainRejection<NativeUuidDto>(user);
    expectUuidDomainRejection<UserIdDto>(native);
    expectMatchingUuidDomain<UserIdDto>(user);
}

TEST(ProjectionQueryTest, runtimeAliasesPreserveMatchingUuidDomainsAcrossOptionality)
{
    const auto required = as("id", col<&UuidRecord::id>());
    const auto optional = as("id", col<&UuidRecord::optionalId>());
    expectMatchingUuidDomain<UserIdDto>(required);
    expectMatchingUuidDomain<OptionalUserIdDto>(required.dynamic());
    expectMatchingUuidDomain<UserIdDto>(optional);
    expectMatchingUuidDomain<OptionalUserIdDto>(optional.dynamic());
}

TEST(ProjectionQueryTest, shouldAllowOneUuidColumnProjectedUnderDifferentAliases)
{
    orm::ProjectionQuery<UuidRecord, RepeatedUserIdDto> query;
    ASSERT_NO_THROW(query.project(as("id", col<&UuidRecord::id>()), as("copy", col<&UuidRecord::id>())));

    const auto& spec = orm::detail::QueryTestAccess::getSelectSpec(query);
    ASSERT_EQ(spec.projections.size(), 2U);
    EXPECT_EQ(spec.projections[0].resultField, "id");
    EXPECT_EQ(spec.projections[1].resultField, "copy");
    EXPECT_EQ(projectionColumnPath(spec.projections[0]), "id");
    EXPECT_EQ(projectionColumnPath(spec.projections[1]), "id");
}

TEST(ProjectionQueryTest, shouldRejectDuplicateAliasesForTheSameUuidColumn)
{
    orm::ProjectionQuery<UuidRecord, UserIdDto> query;
    EXPECT_THAT([&] { query.project(as("id", col<&UuidRecord::id>()), as("id", col<&UuidRecord::id>())); },
                ::testing::ThrowsMessage<std::invalid_argument>("Duplicate projection alias: id"));
}

TEST(ProjectionQueryTest, shouldCheckTheUuidDomainOfEachProjectedAlias)
{
    orm::ProjectionQuery<UuidRecord, MixedUuidDto> query;
    EXPECT_THAT(
        [&] { query.project(as("id", col<&UuidRecord::id>()), as("copy", col<&UuidRecord::id>())); },
        ::testing::ThrowsMessage<std::invalid_argument>("Projection UUID domain type does not match the result field"));
}

TEST(ProjectionQueryTest, uuidProjectionReadPropagatesAllocationFailureWithoutReplacingTheField)
{
    expectProjectionReadAllocationFailure(orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff"));
}

TEST(ProjectionQueryTest, adaptedUuidProjectionReadPropagatesAllocationFailureWithoutReplacingTheField)
{
    expectProjectionReadAllocationFailure(UserId{orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff")});
}

TEST(ProjectionQueryTest, optionalUuidProjectionReadPropagatesAllocationFailureWithoutReplacingTheField)
{
    expectProjectionReadAllocationFailure(
        std::optional{UserId{orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff")}});
}

TEST(ProjectionQueryTest, uuidProjectionReadStillWrapsIncompatibleRepresentations)
{
    const FailingProjectionValues<std::bad_cast> values;
    const std::string alias{"id"};
    const auto original = orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff");
    auto field = original;

    EXPECT_THAT([&] { orm::db::binding::ObjectFieldFromProjectionValues<orm::Uuid>::get(&field, alias, values); },
                ::testing::ThrowsMessage<orm::db::binding::ConversionError>(
                    "A database value has an incompatible result representation"));
    EXPECT_EQ(field, original);
    EXPECT_EQ(values.reads, 1U);
    EXPECT_EQ(values.requestedField, &alias);
}
