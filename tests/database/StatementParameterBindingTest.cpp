#include <cstdint>
#include <format>
#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "orm-cxx/database.hpp"
#include "orm-cxx/database/sqlite/SqliteBackend.hpp"
#include "soci/values.h"
#include "tests/ModelsDefinitions.hpp"

namespace binding_test_models
{
struct ModelOptionallyRelatedToCompositeIdModel
{
    int id;
    std::optional<models::ModelWithOverwrittenId> field1;
};

struct ModelWithLongId
{
    long id;
    long value;
};

struct ModelRelatedToLongId
{
    int id;
    ModelWithLongId relation;
};
} // namespace binding_test_models

namespace
{
auto nullParameter(orm::model::ColumnType type) -> orm::db::StatementParameter
{
    return orm::db::StatementParameter{.name = "value", .value = std::nullopt, .nullType = type};
}
} // namespace

static_assert(std::is_aggregate_v<orm::db::StatementParameter>);

TEST(StatementParameterBindingTest, shouldBindSupportedNullParameterTypes)
{
    const auto supportedTypes = std::vector<orm::model::ColumnType>{
        orm::model::ColumnType::Bool,          orm::model::ColumnType::Char,
        orm::model::ColumnType::UnsignedChar,  orm::model::ColumnType::Short,
        orm::model::ColumnType::UnsignedShort, orm::model::ColumnType::Int,
        orm::model::ColumnType::UnsignedInt,   orm::model::ColumnType::UnsignedLongLong,
        orm::model::ColumnType::LongLong,      orm::model::ColumnType::Float,
        orm::model::ColumnType::Double,        orm::model::ColumnType::String,
    };

    for (const auto type : supportedTypes)
    {
        soci::values values;

        EXPECT_NO_THROW(orm::db::binding::bindStatementParameter(values, nullParameter(type)));
        EXPECT_EQ(values.get_indicator("value"), soci::i_null);
    }
}

TEST(StatementParameterBindingTest, backendRuntimeBoundNullShouldRoundTripThroughSqlite)
{
    const orm::db::sqlite::SqliteBackend backend;
    auto session = soci::session{};
    auto values = soci::values{};

    session.open("sqlite3://:memory:");
    session << "CREATE TABLE nullable_value (value TEXT NULL);";
    backend.runtime().bind(values, "value",
                           orm::db::BoundValue{.logicalType = orm::model::ColumnType::String, .value = std::nullopt});
    ASSERT_EQ(values.get_indicator("value"), soci::i_null);
    session << "INSERT INTO nullable_value (value) VALUES (:value);", soci::use(values);

    auto selected = std::string{};
    auto indicator = soci::i_ok;
    session << "SELECT value FROM nullable_value;", soci::into(selected, indicator);

    EXPECT_EQ(indicator, soci::i_null);
}

TEST(StatementParameterBindingTest, shouldRejectUnsupportedNullParameterTypes)
{
    const auto unsupportedTypes = std::vector<orm::model::ColumnType>{
        orm::model::ColumnType::Uuid,
        orm::model::ColumnType::Unknown,
        orm::model::ColumnType::OneToOne,
        static_cast<orm::model::ColumnType>(999), // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    };

    for (const auto type : unsupportedTypes)
    {
        soci::values values;

        EXPECT_THROW(orm::db::binding::bindStatementParameter(values, nullParameter(type)), std::invalid_argument);
    }
}

TEST(StatementParameterBindingTest, shouldBindSupportedObjectFieldNullValueTypes)
{
    const auto supportedTypes = std::vector<orm::model::ColumnType>{
        orm::model::ColumnType::Bool,          orm::model::ColumnType::Char,
        orm::model::ColumnType::UnsignedChar,  orm::model::ColumnType::Short,
        orm::model::ColumnType::UnsignedShort, orm::model::ColumnType::Int,
        orm::model::ColumnType::UnsignedInt,   orm::model::ColumnType::UnsignedLongLong,
        orm::model::ColumnType::LongLong,      orm::model::ColumnType::Float,
        orm::model::ColumnType::Double,        orm::model::ColumnType::String,
    };

    for (const auto type : supportedTypes)
    {
        auto values = soci::values{};

        EXPECT_NO_THROW(orm::db::binding::setNullValue(values, "value", type));
        EXPECT_EQ(values.get_indicator("value"), soci::i_null);
    }
}

TEST(StatementParameterBindingTest, shouldRejectUnsupportedObjectFieldNullValueTypes)
{
    const auto unsupportedTypes = std::vector<orm::model::ColumnType>{
        orm::model::ColumnType::Uuid,
        orm::model::ColumnType::Unknown,
        orm::model::ColumnType::OneToOne,
        static_cast<orm::model::ColumnType>(999), // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    };

    for (const auto type : unsupportedTypes)
    {
        auto values = soci::values{};

        EXPECT_THROW(orm::db::binding::setNullValue(values, "value", type), std::invalid_argument);
    }
}

TEST(StatementParameterBindingTest, shouldBindPresentParameterValue)
{
    soci::values values;
    const auto parameter = orm::db::StatementParameter{.name = "value", .value = orm::query::QueryValue{42}};

    EXPECT_NO_THROW(orm::db::binding::bindStatementParameter(values, parameter));
}

TEST(StatementParameterBindingTest, backendRuntimeShouldReceiveAndBindLogicalStatementValue)
{
    const orm::db::sqlite::SqliteBackend backend;
    soci::values values;
    const auto parameter = orm::db::StatementParameter{.name = "value", .value = orm::query::QueryValue{true}};

    EXPECT_NO_THROW(orm::detail::bindStatementParameter(backend.runtime(), values, parameter));
    EXPECT_EQ(values.get<int>("value"), 1);
}

TEST(StatementParameterBindingTest, backendRuntimeShouldRejectInconsistentLogicalStorage)
{
    const orm::db::sqlite::SqliteBackend backend;
    soci::values values;
    const auto inconsistent = orm::db::BoundValue{
        .logicalType = orm::model::ColumnType::String,
        .value = orm::query::QueryValue::Value{42},
    };

    EXPECT_THROW(backend.runtime().bind(values, "value", inconsistent), orm::db::binding::ConversionError);
}

TEST(StatementParameterBindingTest, backendRuntimeShouldRejectLogicalValueOutsideItsRange)
{
    const orm::db::sqlite::SqliteBackend backend;
    soci::values values;
    const auto inconsistent = orm::db::BoundValue{
        .logicalType = orm::model::ColumnType::Bool,
        .value = orm::query::QueryValue::Value{2},
    };

    EXPECT_THROW(backend.runtime().bind(values, "value", inconsistent), orm::db::binding::ConversionError);
}

TEST(StatementParameterBindingTest, presentParameterShouldExposeBoundValueWithItsLogicalType)
{
    const auto parameter = orm::db::StatementParameter{
        .name = "value",
        .value = orm::query::QueryValue{true},
        .nullType = orm::model::ColumnType::String,
    };

    const auto boundValue = parameter.getBoundValue();

    EXPECT_EQ(parameter.getLogicalType(), orm::model::ColumnType::Bool);
    EXPECT_EQ(boundValue.logicalType, orm::model::ColumnType::Bool);
    ASSERT_FALSE(boundValue.isNull());
    ASSERT_TRUE(boundValue.value.has_value());
    EXPECT_EQ(std::get<int>(boundValue.value.value()), 1);
}

TEST(StatementParameterBindingTest, nullParameterShouldExposeBoundValueWithItsDeclaredLogicalType)
{
    const auto parameter = nullParameter(orm::model::ColumnType::Double);

    const auto boundValue = parameter.getBoundValue();

    EXPECT_EQ(parameter.getLogicalType(), orm::model::ColumnType::Double);
    EXPECT_EQ(boundValue.logicalType, orm::model::ColumnType::Double);
    EXPECT_TRUE(boundValue.isNull());
    EXPECT_FALSE(boundValue.value.has_value());
}

TEST(StatementParameterBindingTest, shouldBindAllPresentParameterValueVariants)
{
    const auto parameters = std::vector<orm::db::StatementParameter>{
        {.name = "intValue", .value = orm::query::QueryValue{42}},
        {.name = "longLongValue", .value = orm::query::QueryValue{42LL}},
        {.name = "unsignedLongLongValue", .value = orm::query::QueryValue{42ULL}},
        {.name = "doubleValue", .value = orm::query::QueryValue{4.2}},
        {.name = "stringValue", .value = orm::query::QueryValue{std::string{"value"}}},
    };
    soci::values values;

    for (const auto& parameter : parameters)
    {
        EXPECT_NO_THROW(orm::db::binding::bindStatementParameter(values, parameter));
    }
}

TEST(StatementParameterBindingTest, shouldBindModelFieldsAndRelatedModelPrimaryKey)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelRelatedToOtherModel>;

    const auto model = models::ModelRelatedToOtherModel{.id = 1,
                                                        .field1 = 2,
                                                        .field2 = "parent",
                                                        .field3 = models::ModelWithId{
                                                            .id = 3,
                                                            .field1 = 4,
                                                            .field2 = "foreign",
                                                        }};
    const auto payload = Payload{.value = model};
    auto values = soci::values{};
    auto indicator = soci::indicator{};

    soci::type_conversion<Payload>::to_base(payload, values, indicator);

    EXPECT_EQ(indicator, soci::i_ok);
    EXPECT_EQ(values.get<int>("id"), model.id);
    EXPECT_EQ(values.get<int>("field1"), model.field1);
    EXPECT_EQ(values.get<std::string>("field2"), model.field2);
    EXPECT_EQ(values.get<int>("field3_id"), model.field3.id);
}

TEST(StatementParameterBindingTest, shouldBindModelWithOverwrittenIdFields)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithOverwrittenId>;

    const auto model = models::ModelWithOverwrittenId{.id = 1, .field1 = 2, .field2 = "composite"};
    const auto payload = Payload{.value = model};
    auto values = soci::values{};
    auto indicator = soci::indicator{};

    soci::type_conversion<Payload>::to_base(payload, values, indicator);

    EXPECT_EQ(indicator, soci::i_ok);
    EXPECT_EQ(values.get<int>("id"), model.id);
    EXPECT_EQ(values.get<int>("field1"), model.field1);
    EXPECT_EQ(values.get<std::string>("field2"), model.field2);
}

TEST(StatementParameterBindingTest, shouldHydrateModelWithOverwrittenIdFields)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithOverwrittenId>;

    const auto payload = Payload{};
    auto values = soci::values{};
    auto id = int{};
    auto field1 = int{};
    auto field2 = std::string{};

    values.set("models_ModelWithOverwrittenId_id", 1);
    values.set("models_ModelWithOverwrittenId_field1", 2);
    values.set("models_ModelWithOverwrittenId_field2", std::string{"composite"});

    orm::db::binding::ObjectFieldFromValues<int>::get(&id, payload, 0, values);
    orm::db::binding::ObjectFieldFromValues<int>::get(&field1, payload, 1, values);
    orm::db::binding::ObjectFieldFromValues<std::string>::get(&field2, payload, 2, values);

    EXPECT_EQ(id, 1);
    EXPECT_EQ(field1, 2);
    EXPECT_EQ(field2, "composite");
}

TEST(StatementParameterBindingTest, shouldHydrateUnsignedLongLongConvertedModelField)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithAllInts>;

    const auto payload = Payload{};
    auto values = soci::values{};
    auto field = std::uint64_t{};

    values.set("all_ints_field8", static_cast<unsigned long long>(42));

    orm::db::binding::ObjectFieldFromValues<std::uint64_t>::get(&field, payload, 7, values);

    EXPECT_EQ(field, 42);
}

TEST(StatementParameterBindingTest, shouldRejectNonFiniteFullModelField)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithAllBasicTypes>;

    const auto payload = Payload{};
    auto values = soci::values{};
    auto field = double{};

    values.set("all_types_field13", std::numeric_limits<double>::infinity());

    EXPECT_THROW(orm::db::binding::ObjectFieldFromValues<double>::get(&field, payload, 13, values),
                 orm::db::binding::ConversionError);
}

TEST(StatementParameterBindingTest, shouldRejectOutOfRangeHydratedRelationPrimaryKey)
{
    auto values = soci::values{};
    values.set("owner_key", 2);

    EXPECT_THROW((void)orm::db::binding::getPrimaryKeyValue(values, "owner_key", orm::model::ColumnType::Bool),
                 orm::db::binding::ConversionError);
}

TEST(StatementParameterBindingTest, shouldRoundTripPlatformLongWithoutNarrowing)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithAllBasicTypes>;

    const auto payload = Payload{};
    const auto input = std::numeric_limits<long>::max();
    auto boundValues = soci::values{};

    orm::db::binding::ObjectFieldToValues<long>::set(&input, payload, 8, boundValues);

    auto selectedValues = soci::values{};

    if constexpr (sizeof(long) > sizeof(int))
    {
        EXPECT_EQ(boundValues.get<long long>("field8"), static_cast<long long>(input));
        selectedValues.set("all_types_field8", static_cast<long long>(input));
        EXPECT_EQ(payload.getModelInfo().columnsInfo[8].type, orm::model::ColumnType::LongLong);
    }
    else
    {
        EXPECT_EQ(boundValues.get<int>("field8"), static_cast<int>(input));
        selectedValues.set("all_types_field8", static_cast<int>(input));
        EXPECT_EQ(payload.getModelInfo().columnsInfo[8].type, orm::model::ColumnType::Int);
    }

    auto output = long{};
    orm::db::binding::ObjectFieldFromValues<long>::get(&output, payload, 8, selectedValues);
    EXPECT_EQ(output, input);
}

TEST(StatementParameterBindingTest, shouldRoundTripPlatformLongInRelatedModelWithoutNarrowing)
{
    using Model = binding_test_models::ModelRelatedToLongId;
    using Payload = orm::db::binding::BindingPayload<Model>;
    using JoinedPayload = orm::db::binding::BindingPayload<Model, true>;

    const auto input = std::numeric_limits<long>::max();
    const auto payload = Payload{.value = Model{.id = 1, .relation = {.id = input, .value = input}}};
    const auto& modelInfo = payload.getModelInfo();
    const auto& relationColumn = modelInfo.columnsInfo[1];
    const auto& relatedModelInfo = modelInfo.foreignModelsInfo.at(relationColumn.name);
    const auto& idColumn = relatedModelInfo.columnsInfo[0];
    const auto& valueColumn = relatedModelInfo.columnsInfo[1];
    auto boundValues = soci::values{};

    orm::db::binding::ObjectFieldToValues<binding_test_models::ModelWithLongId>::set(&payload.value.relation, payload,
                                                                                     1, boundValues);

    const auto boundIdName = std::format("{}_{}", relationColumn.name, idColumn.name);

    if constexpr (sizeof(long) > sizeof(int))
    {
        EXPECT_EQ(boundValues.get<long long>(boundIdName), static_cast<long long>(input));
    }
    else
    {
        EXPECT_EQ(boundValues.get<int>(boundIdName), static_cast<int>(input));
    }

    auto setLongValue = [input](soci::values& values, const std::string& name)
    {
        if constexpr (sizeof(long) > sizeof(int))
        {
            values.set(name, static_cast<long long>(input));
        }
        else
        {
            values.set(name, static_cast<int>(input));
        }
    };

    auto selectedValues = soci::values{};
    setLongValue(selectedValues, std::format("{}_{}_{}", modelInfo.tableName, relationColumn.name, idColumn.name));
    auto relatedModel = binding_test_models::ModelWithLongId{};
    orm::db::binding::ObjectFieldFromValues<binding_test_models::ModelWithLongId>::get(&relatedModel, payload, 1,
                                                                                       selectedValues);
    EXPECT_EQ(relatedModel.id, input);

    const auto joinedPayload = JoinedPayload{};
    auto joinedValues = soci::values{};
    setLongValue(joinedValues, std::format("{}_{}", relationColumn.name, idColumn.name));
    setLongValue(joinedValues, std::format("{}_{}", relationColumn.name, valueColumn.name));
    relatedModel = {};
    orm::db::binding::ObjectFieldFromValues<binding_test_models::ModelWithLongId>::get(&relatedModel, joinedPayload, 1,
                                                                                       joinedValues);
    EXPECT_EQ(relatedModel.id, input);
    EXPECT_EQ(relatedModel.value, input);
}

TEST(StatementParameterBindingTest, shouldRoundTripPlatformUnsignedLongWithoutNarrowing)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithAllBasicTypes>;

    const auto payload = Payload{};
    const auto input = std::numeric_limits<unsigned long>::max();
    auto boundValues = soci::values{};

    orm::db::binding::ObjectFieldToValues<unsigned long>::set(&input, payload, 9, boundValues);
    EXPECT_EQ(boundValues.get<unsigned long long>("field9"), static_cast<unsigned long long>(input));

    auto selectedValues = soci::values{};
    selectedValues.set("all_types_field9", static_cast<unsigned long long>(input));
    auto output = static_cast<unsigned long>(0);
    orm::db::binding::ObjectFieldFromValues<unsigned long>::get(&output, payload, 9, selectedValues);

    const auto expectedType = sizeof(unsigned long) > sizeof(unsigned int) ? orm::model::ColumnType::UnsignedLongLong :
                                                                             orm::model::ColumnType::UnsignedInt;
    EXPECT_EQ(payload.getModelInfo().columnsInfo[9].type, expectedType);
    EXPECT_EQ(output, input);
}

TEST(StatementParameterBindingTest, fixedWidth64BitFieldsUse64BitSociStorage)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithAllInts>;

    const auto payload = Payload{};
    const auto signedInput = std::numeric_limits<std::int64_t>::max();
    const auto unsignedInput = std::numeric_limits<std::uint64_t>::max();
    auto boundValues = soci::values{};

    orm::db::binding::ObjectFieldToValues<std::int64_t>::set(&signedInput, payload, 6, boundValues);
    orm::db::binding::ObjectFieldToValues<std::uint64_t>::set(&unsignedInput, payload, 7, boundValues);

    EXPECT_EQ(boundValues.get<long long>("field7"), static_cast<long long>(signedInput));
    EXPECT_EQ(boundValues.get<unsigned long long>("field8"), static_cast<unsigned long long>(unsignedInput));
    EXPECT_EQ(payload.getModelInfo().columnsInfo[6].type, orm::model::ColumnType::LongLong);
    EXPECT_EQ(payload.getModelInfo().columnsInfo[7].type, orm::model::ColumnType::UnsignedLongLong);

    auto selectedValues = soci::values{};
    selectedValues.set("all_ints_field7", static_cast<long long>(signedInput));
    selectedValues.set("all_ints_field8", static_cast<unsigned long long>(unsignedInput));
    auto signedOutput = std::int64_t{};
    auto unsignedOutput = std::uint64_t{};

    orm::db::binding::ObjectFieldFromValues<std::int64_t>::get(&signedOutput, payload, 6, selectedValues);
    orm::db::binding::ObjectFieldFromValues<std::uint64_t>::get(&unsignedOutput, payload, 7, selectedValues);

    EXPECT_EQ(signedOutput, signedInput);
    EXPECT_EQ(unsignedOutput, unsignedInput);
}

TEST(StatementParameterBindingTest, shouldBindNullOptionalScalarFields)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithOptional>;

    const auto model =
        models::ModelWithOptional{.field1 = std::nullopt, .field2 = std::nullopt, .field3 = std::nullopt};
    const auto payload = Payload{.value = model};
    auto values = soci::values{};
    auto indicator = soci::indicator{};

    EXPECT_NO_THROW(soci::type_conversion<Payload>::to_base(payload, values, indicator));

    EXPECT_EQ(indicator, soci::i_ok);
    EXPECT_EQ(values.get_indicator("field1"), soci::i_null);
    EXPECT_EQ(values.get_indicator("field2"), soci::i_null);
    EXPECT_EQ(values.get_indicator("field3"), soci::i_null);
}

TEST(StatementParameterBindingTest, shouldBindNullOptionalRelationPrimaryKey)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelOptionallyRelatedToOtherModel>;

    const auto model = models::ModelOptionallyRelatedToOtherModel{
        .id = 1, .field1 = 2, .field2 = "without-relation", .field3 = std::nullopt};
    const auto payload = Payload{.value = model};
    auto values = soci::values{};
    auto indicator = soci::indicator{};

    EXPECT_NO_THROW(soci::type_conversion<Payload>::to_base(payload, values, indicator));

    EXPECT_EQ(indicator, soci::i_ok);
    EXPECT_EQ(values.get_indicator("field3_id"), soci::i_null);
}

TEST(StatementParameterBindingTest, shouldHydratePresentOptionalCompositeRelation)
{
    using Payload = orm::db::binding::BindingPayload<binding_test_models::ModelOptionallyRelatedToCompositeIdModel>;

    auto payload = Payload{};
    auto values = soci::values{};
    auto relation = std::optional<models::ModelWithOverwrittenId>{};
    const auto& modelInfo = payload.getModelInfo();
    const auto& relationInfo = modelInfo.columnsInfo[1];

    values.set(std::format("{}_{}_field1", modelInfo.tableName, relationInfo.name), 7);
    values.set(std::format("{}_{}_field2", modelInfo.tableName, relationInfo.name), std::string{"composite"});

    orm::db::binding::ObjectFieldFromValues<std::optional<models::ModelWithOverwrittenId>>::get(&relation, payload, 1,
                                                                                                values);

    ASSERT_TRUE(relation.has_value());
    EXPECT_EQ(relation->id, 0);
    EXPECT_EQ(relation->field1, 7);
    EXPECT_EQ(relation->field2, "composite");
}

TEST(StatementParameterBindingTest, shouldRejectPartiallyNullOptionalCompositeRelation)
{
    using Payload = orm::db::binding::BindingPayload<binding_test_models::ModelOptionallyRelatedToCompositeIdModel>;

    auto payload = Payload{};
    auto values = soci::values{};
    auto relation = std::optional<models::ModelWithOverwrittenId>{};
    const auto& modelInfo = payload.getModelInfo();
    const auto& relationInfo = modelInfo.columnsInfo[1];
    const auto nullCompositeIdFieldName = std::format("{}_{}_field2", modelInfo.tableName, relationInfo.name);

    values.set(std::format("{}_{}_field1", modelInfo.tableName, relationInfo.name), 7);
    values.set(nullCompositeIdFieldName, std::string{});
    values.set(nullCompositeIdFieldName, std::string{}, soci::i_null);

    EXPECT_THROW(orm::db::binding::ObjectFieldFromValues<std::optional<models::ModelWithOverwrittenId>>::get(
                     &relation, payload, 1, values),
                 std::runtime_error);
}

TEST(StatementParameterBindingTest, shouldNormalizeReportedAffectedRows)
{
    EXPECT_EQ(orm::detail::normalizeAffectedRows(3), 3);
}

TEST(StatementParameterBindingTest, shouldRejectNegativeAffectedRows)
{
    EXPECT_THROW((void)orm::detail::normalizeAffectedRows(-1), std::runtime_error);
}
