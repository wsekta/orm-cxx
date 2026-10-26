module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :foundation;
import :value_codec;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

using namespace orm::test::fixtures;

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

using Schema = orm::Schema<models::ModelWithId, models::ModelRelatedToOtherModel, models::ModelWithOverwrittenId,
                           models::ModelWithAllInts, models::ModelWithOptional, models::ModelWithAllBasicTypes,
                           models::ModelOptionallyRelatedToOtherModel, ModelOptionallyRelatedToCompositeIdModel,
                           ModelWithLongId, ModelRelatedToLongId>;
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
        orm::model::ColumnType::Date,          orm::model::ColumnType::TimeOfDay,
        orm::model::ColumnType::LocalDateTime, orm::model::ColumnType::UtcTimestamp,
        orm::model::ColumnType::Uuid,
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
        static_cast<orm::model::ColumnType>(999), // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    };

    for (const auto type : unsupportedTypes)
    {
        soci::values values;

        EXPECT_THROW(orm::db::binding::bindStatementParameter(values, nullParameter(type)), std::invalid_argument);
    }
}

TEST(StatementParameterBindingTest, shouldRequireLogicalTypeForNullParameter)
{
    const auto parameter =
        orm::db::StatementParameter{.name = "value", .value = std::nullopt, .nullType = std::nullopt};

    EXPECT_THROW((void)parameter.getBoundValue(), std::invalid_argument);
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
        orm::model::ColumnType::Date,          orm::model::ColumnType::TimeOfDay,
        orm::model::ColumnType::LocalDateTime, orm::model::ColumnType::UtcTimestamp,
        orm::model::ColumnType::Uuid,
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

TEST(StatementParameterBindingTest, nullBoundValueShouldAlwaysHaveCompatibleStorage)
{
    const auto value = orm::db::BoundValue{
        .logicalType = orm::model::ColumnType::Uuid,
        .value = std::nullopt,
    };

    EXPECT_TRUE(orm::db::binding::hasCompatibleStorage(value));
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
    using Payload = orm::db::binding::BindingPayload<models::ModelRelatedToOtherModel, binding_test_models::Schema>;

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
    using Payload = orm::db::binding::BindingPayload<models::ModelWithOverwrittenId, binding_test_models::Schema>;

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
    using Payload = orm::db::binding::BindingPayload<models::ModelWithOverwrittenId, binding_test_models::Schema>;

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
    using Payload = orm::db::binding::BindingPayload<models::ModelWithAllInts, binding_test_models::Schema>;

    const auto payload = Payload{};
    auto values = soci::values{};
    auto field = std::uint64_t{};

    values.set("all_ints_field8", static_cast<unsigned long long>(42));

    orm::db::binding::ObjectFieldFromValues<std::uint64_t>::get(&field, payload, 7, values);

    EXPECT_EQ(field, 42);
}

TEST(StatementParameterBindingTest, shouldHydrateUnsignedLongLongFieldFromSignedDriverStorage)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithAllInts, binding_test_models::Schema>;

    const auto payload = Payload{};
    auto values = soci::values{};
    auto field = std::uint64_t{};

    values.set("all_ints_field8", static_cast<long long>(42));

    orm::db::binding::ObjectFieldFromValues<std::uint64_t>::get(&field, payload, 7, values);

    EXPECT_EQ(field, 42);
}

TEST(StatementParameterBindingTest, numericHydrationUsesTheDriverDescribedHolderType)
{
    const orm::db::sqlite::SqliteBackend backend;
    auto session = soci::session{};
    const auto config = orm::SqliteConfig{.database = orm::SqliteMemory{}};
    backend.runtime().open(session, orm::db::ConnectionConfigView{config});
    session << "CREATE TABLE holder_probe (value BIGINT NOT NULL);";
    session << "INSERT INTO holder_probe (value) VALUES (42);";

    auto values = soci::values{};
    soci::statement statement =
        (session.prepare << "SELECT value AS numeric_value FROM holder_probe;", soci::into(values));
    statement.execute(true);

    ASSERT_EQ(values.get_properties("numeric_value").get_data_type(), soci::dt_long_long);
    EXPECT_EQ(orm::db::binding::getNumericValue<unsigned long long>(values, "numeric_value"), 42);
}

TEST(StatementParameterBindingTest, numericHydrationRejectsNonNumericDriverDescribedTypes)
{
    const orm::db::sqlite::SqliteBackend backend;
    auto session = soci::session{};
    const auto config = orm::SqliteConfig{.database = orm::SqliteMemory{}};
    backend.runtime().open(session, orm::db::ConnectionConfigView{config});
    session << "CREATE TABLE non_numeric_probe (date_value DATE, blob_value BLOB);";
    session << "INSERT INTO non_numeric_probe (date_value, blob_value) VALUES ('2026-07-24', X'01');";

    auto values = soci::values{};
    soci::statement statement =
        (session.prepare << "SELECT date_value, blob_value FROM non_numeric_probe;", soci::into(values));
    statement.execute(true);

    ASSERT_EQ(values.get_properties("date_value").get_data_type(), soci::dt_date);
    ASSERT_EQ(values.get_properties("blob_value").get_data_type(), soci::dt_blob);
    EXPECT_THROW((void)orm::db::binding::getNumericValue<int>(values, "date_value"), orm::db::binding::ConversionError);
    EXPECT_THROW((void)orm::db::binding::getNumericValue<int>(values, "blob_value"), orm::db::binding::ConversionError);
}

TEST(StatementParameterBindingTest, shouldHydrateOptionalScalarFromWiderDriverStorage)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithOptional, binding_test_models::Schema>;

    const auto payload = Payload{};
    auto values = soci::values{};
    auto field = std::optional<int>{};

    values.set("models_ModelWithOptional_field1", static_cast<long long>(42));

    orm::db::binding::ObjectFieldFromValues<std::optional<int>>::get(&field, payload, 0, values);

    EXPECT_EQ(field, 42);
}

TEST(StatementParameterBindingTest, shouldRejectNonFiniteFullModelField)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithAllBasicTypes, binding_test_models::Schema>;

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

TEST(StatementParameterBindingTest, shouldHydrateUnsignedRelationPrimaryKeyFromSignedDriverStorage)
{
    auto values = soci::values{};
    values.set("owner_key", static_cast<long long>(42));

    const auto key =
        orm::db::binding::getPrimaryKeyValue(values, "owner_key", orm::model::ColumnType::UnsignedLongLong);

    EXPECT_EQ(std::get<unsigned long long>(key.get()), 42);
}

TEST(StatementParameterBindingTest, shouldRoundTripPlatformLongWithoutNarrowing)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithAllBasicTypes, binding_test_models::Schema>;

    const auto payload = Payload{};
    const auto input = std::numeric_limits<long>::max();
    auto boundValues = soci::values{};

    orm::db::binding::ObjectFieldToValues<long>::set(&input, payload, 8, boundValues);

    auto selectedValues = soci::values{};

    if constexpr (sizeof(long) > sizeof(int))
    {
        EXPECT_EQ(boundValues.get<long long>("field8"), static_cast<long long>(input));
        selectedValues.set("all_types_field8", static_cast<long long>(input));
        EXPECT_EQ(payload.columnDescriptor(8).type, orm::model::ColumnType::LongLong);
    }
    else
    {
        EXPECT_EQ(boundValues.get<int>("field8"), static_cast<int>(input));
        selectedValues.set("all_types_field8", static_cast<int>(input));
        EXPECT_EQ(payload.columnDescriptor(8).type, orm::model::ColumnType::Int);
    }

    auto output = long{};
    orm::db::binding::ObjectFieldFromValues<long>::get(&output, payload, 8, selectedValues);
    EXPECT_EQ(output, input);
}

TEST(StatementParameterBindingTest, shouldRoundTripPlatformLongInRelatedModelWithoutNarrowing)
{
    using Model = binding_test_models::ModelRelatedToLongId;
    using Payload = orm::db::binding::BindingPayload<Model, binding_test_models::Schema>;
    using JoinedPayload = orm::db::binding::BindingPayload<Model, binding_test_models::Schema, true>;

    const auto input = std::numeric_limits<long>::max();
    const auto payload = Payload{.value = Model{.id = 1, .relation = {.id = input, .value = input}}};
    const auto& modelDescriptor = payload.modelDescriptor();
    const auto& relationColumn = payload.columnDescriptor(1);
    const auto relatedModelDescriptor = modelDescriptor.resolveTarget(relationColumn);
    ASSERT_NE(relatedModelDescriptor, nullptr);
    const auto& idColumn = relatedModelDescriptor->columns[0];
    const auto& valueColumn = relatedModelDescriptor->columns[1];
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
    setLongValue(selectedValues,
                 std::format("{}_{}_{}", modelDescriptor->tableName, relationColumn.name, idColumn.name));
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
    using Payload = orm::db::binding::BindingPayload<models::ModelWithAllBasicTypes, binding_test_models::Schema>;

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
    EXPECT_EQ(payload.columnDescriptor(9).type, expectedType);
    EXPECT_EQ(output, input);
}

TEST(StatementParameterBindingTest, fixedWidth64BitFieldsUse64BitSociStorage)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithAllInts, binding_test_models::Schema>;

    const auto payload = Payload{};
    const auto signedInput = std::numeric_limits<std::int64_t>::max();
    const auto unsignedInput = std::numeric_limits<std::uint64_t>::max();
    auto boundValues = soci::values{};

    orm::db::binding::ObjectFieldToValues<std::int64_t>::set(&signedInput, payload, 6, boundValues);
    orm::db::binding::ObjectFieldToValues<std::uint64_t>::set(&unsignedInput, payload, 7, boundValues);

    EXPECT_EQ(boundValues.get<long long>("field7"), static_cast<long long>(signedInput));
    EXPECT_EQ(boundValues.get<unsigned long long>("field8"), static_cast<unsigned long long>(unsignedInput));
    EXPECT_EQ(payload.columnDescriptor(6).type, orm::model::ColumnType::LongLong);
    EXPECT_EQ(payload.columnDescriptor(7).type, orm::model::ColumnType::UnsignedLongLong);

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
    using Payload = orm::db::binding::BindingPayload<models::ModelWithOptional, binding_test_models::Schema>;

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
    using Payload =
        orm::db::binding::BindingPayload<models::ModelOptionallyRelatedToOtherModel, binding_test_models::Schema>;

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
    using Payload = orm::db::binding::BindingPayload<binding_test_models::ModelOptionallyRelatedToCompositeIdModel,
                                                     binding_test_models::Schema>;

    auto payload = Payload{};
    auto values = soci::values{};
    auto relation = std::optional<models::ModelWithOverwrittenId>{};
    const auto& modelDescriptor = payload.modelDescriptor();
    const auto& relationInfo = payload.columnDescriptor(1);

    values.set(std::format("{}_{}_field1", modelDescriptor->tableName, relationInfo.name), static_cast<long long>(7));
    values.set(std::format("{}_{}_field2", modelDescriptor->tableName, relationInfo.name), std::string{"composite"});

    orm::db::binding::ObjectFieldFromValues<std::optional<models::ModelWithOverwrittenId>>::get(&relation, payload, 1,
                                                                                                values);

    ASSERT_TRUE(relation.has_value());
    EXPECT_EQ(relation->id, 0);
    EXPECT_EQ(relation->field1, 7);
    EXPECT_EQ(relation->field2, "composite");
}

TEST(StatementParameterBindingTest, shouldRejectPartiallyNullOptionalCompositeRelation)
{
    using Payload = orm::db::binding::BindingPayload<binding_test_models::ModelOptionallyRelatedToCompositeIdModel,
                                                     binding_test_models::Schema>;

    auto payload = Payload{};
    auto values = soci::values{};
    auto relation = std::optional<models::ModelWithOverwrittenId>{};
    const auto& modelDescriptor = payload.modelDescriptor();
    const auto& relationInfo = payload.columnDescriptor(1);
    const auto nullCompositeIdFieldName = std::format("{}_{}_field2", modelDescriptor->tableName, relationInfo.name);

    values.set(std::format("{}_{}_field1", modelDescriptor->tableName, relationInfo.name), 7);
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

TEST(StatementParameterBindingTest, runtimeBindingPayloadRejectsAFieldWithoutAColumn)
{
    using Payload = orm::db::binding::BindingPayload<models::ModelWithId, binding_test_models::Schema>;
    EXPECT_THROW((void)Payload::columnDescriptor(99), std::invalid_argument);
}

TEST(StatementParameterBindingTest, malformedPrimaryKeyDescriptorsReportTheirSpecificFailure)
{
    using orm::model::ColumnView;
    using orm::model::FieldKind;

    std::array<ColumnView, 1> columns{{{.fieldIndex = 0,
                                        .fieldName = "id",
                                        .name = "id",
                                        .type = orm::model::ColumnType::Int,
                                        .kind = FieldKind::Scalar}}};
    std::array<std::size_t, 1> primaryKeyIndices{1};
    orm::model::ModelDataView modelData{.type = orm::model::typeId<int>(),
                                        .schemaIndex = 0,
                                        .tableName = "synthetic_key",
                                        .columns = columns,
                                        .primaryKeyIndices = primaryKeyIndices};
    const std::array<const orm::model::ModelDataView*, 1> models{&modelData};
    const orm::model::SchemaView schema{.models = models};
    const auto model = schema.at(0);

    EXPECT_THROW((void)orm::db::binding::getPrimaryKeyColumns(model), std::invalid_argument);
    primaryKeyIndices[0] = 0;
    columns[0].kind = FieldKind::ToOne;
    EXPECT_THROW((void)orm::db::binding::getPrimaryKeyColumns(model), std::invalid_argument);
    columns[0].kind = FieldKind::Scalar;
    modelData.primaryKeyIndices = {};
    EXPECT_THROW((void)orm::db::binding::getPrimaryKeyColumns(model), std::invalid_argument);
}

TEST(StatementParameterBindingTest, incompatibleUuidRelationPrimaryKeyStorageIsRejected)
{
    soci::values values;
    values.set("id", 7);
    EXPECT_THROW((void)orm::db::binding::getPrimaryKeyValue(values, "id", orm::model::ColumnType::Uuid),
                 orm::db::binding::ConversionError);
}

TEST(StatementParameterBindingTest, temporalAndUuidModelFieldsUseValidatedTextStorage)
{
    using namespace std::chrono_literals;
    const orm::Date date{std::chrono::year{2026}, std::chrono::October, std::chrono::day{25}};
    const auto time = orm::TimeOfDay{12h + 34min + 56s + 123456us};
    const auto local = orm::detail::values::parseLocalDateTime("2026-10-25 12:34:56.123456");
    const auto utc = orm::detail::values::parseUtcTimestamp("2026-10-25 12:34:56.000001+00");
    const auto uuid = orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff");
    soci::values values;
    orm::db::binding::setScalarFieldValue(values, "date", date, orm::model::ColumnType::Date);
    orm::db::binding::setScalarFieldValue(values, "time", time, orm::model::ColumnType::TimeOfDay);
    orm::db::binding::setScalarFieldValue(values, "local", local, orm::model::ColumnType::LocalDateTime);
    orm::db::binding::setScalarFieldValue(values, "utc", utc, orm::model::ColumnType::UtcTimestamp);
    orm::db::binding::setScalarFieldValue(values, "uuid", uuid, orm::model::ColumnType::Uuid);

    EXPECT_EQ(values.get<std::string>("date"), "2026-10-25");
    EXPECT_EQ(values.get<std::string>("time"), "12:34:56.123456");
    EXPECT_EQ(values.get<std::string>("local"), "2026-10-25 12:34:56.123456");
    EXPECT_EQ(values.get<std::string>("uuid"), uuid.toString());
    EXPECT_EQ(orm::db::binding::getScalarFieldValue<orm::Date>(values, "date"), date);
    EXPECT_EQ(orm::db::binding::getScalarFieldValue<orm::TimeOfDay>(values, "time"), time);
    EXPECT_EQ(orm::db::binding::getScalarFieldValue<orm::LocalDateTime>(values, "local"), local);
    EXPECT_EQ(orm::db::binding::getScalarFieldValue<orm::UtcTimestamp>(values, "utc"), utc);
    EXPECT_EQ(orm::db::binding::getScalarFieldValue<orm::Uuid>(values, "uuid"), uuid);
    EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "uuid", orm::model::ColumnType::Uuid),
              orm::query::QueryValue{uuid});
    EXPECT_EQ(orm::db::binding::getPrimaryKeyValue(values, "local", orm::model::ColumnType::LocalDateTime),
              orm::query::QueryValue{local});
}

TEST(StatementParameterBindingTest, invalidTemporalAndUuidValuesSignalConversionWithoutLeakingInput)
{
    const orm::Date invalid{std::chrono::year{2026}, std::chrono::February, std::chrono::day{30}};
    soci::values values;
    EXPECT_THROW(orm::db::binding::setScalarFieldValue(values, "date", invalid, orm::model::ColumnType::Date),
                 orm::db::binding::ConversionError);
    values.set("date", std::string{"0000-00-00"});
    values.set("time", std::string{"24:00:00"});
    values.set("local", std::string{"2026-02-30 12:00:00.123456"});
    values.set("utc", std::string{"infinity"});
    values.set("uuid", std::string{"private-invalid-uuid"});
    EXPECT_THROW((void)orm::db::binding::getScalarFieldValue<orm::Date>(values, "date"),
                 orm::db::binding::ConversionError);
    EXPECT_THROW((void)orm::db::binding::getScalarFieldValue<orm::TimeOfDay>(values, "time"),
                 orm::db::binding::ConversionError);
    EXPECT_THROW((void)orm::db::binding::getScalarFieldValue<orm::LocalDateTime>(values, "local"),
                 orm::db::binding::ConversionError);
    EXPECT_THROW((void)orm::db::binding::getScalarFieldValue<orm::UtcTimestamp>(values, "utc"),
                 orm::db::binding::ConversionError);
    try
    {
        (void)orm::db::binding::getScalarFieldValue<orm::Uuid>(values, "uuid");
        FAIL() << "Expected conversion failure";
    }
    catch (const orm::db::binding::ConversionError& error)
    {
        EXPECT_EQ(std::string{error.what()}.find("private-invalid-uuid"), std::string::npos);
    }
}

TEST(StatementParameterBindingTest, explicitResultBuffersPreserveFractionsAcrossNullAndRepeatedFetch)
{
    soci::session session;
    orm::db::sqlite::SqliteBackend backend;
    const orm::SqliteConfig config{.database = orm::SqliteMemory{}};
    backend.runtime().open(session, orm::db::ConnectionConfigView{config});
    orm::db::binding::TypedResultStorage storage{{{"time", orm::model::ColumnType::TimeOfDay},
                                                  {"local", orm::model::ColumnType::LocalDateTime},
                                                  {"number", orm::model::ColumnType::Int}}};
    soci::values parameters;
    std::vector<std::optional<orm::TimeOfDay>> times;
    std::vector<orm::LocalDateTime> localTimes;
    storage.read(session,
                 "SELECT '12:34:56.123456', '2026-10-25 12:34:56.000001', 1 "
                 "UNION ALL SELECT NULL, '2026-10-25 12:34:56.123456', 2 "
                 "UNION ALL SELECT '12:34:56.000001', '2026-10-25 12:34:56.123457', 3",
                 parameters,
                 [&](const auto& row)
                 {
                     times.push_back(orm::db::binding::getScalarFieldValue<std::optional<orm::TimeOfDay>>(row, "time"));
                     localTimes.push_back(orm::db::binding::getScalarFieldValue<orm::LocalDateTime>(row, "local"));
                     EXPECT_EQ(orm::db::binding::getNumericValue<int>(row, "number"), static_cast<int>(times.size()));
                 });
    ASSERT_EQ(times.size(), 3);
    ASSERT_TRUE(times[0]);
    EXPECT_EQ(*times[0], orm::detail::values::parseTimeOfDay("12:34:56.123456"));
    EXPECT_FALSE(times[1]);
    ASSERT_TRUE(times[2]);
    EXPECT_EQ(*times[2], orm::detail::values::parseTimeOfDay("12:34:56.000001"));
    EXPECT_EQ(localTimes[0], orm::detail::values::parseLocalDateTime("2026-10-25 12:34:56.000001"));
    EXPECT_EQ(localTimes[2] - localTimes[1], std::chrono::microseconds{1});
}

TEST(StatementParameterBindingTest, explicitResultBuffersRejectNativeAndEffectiveRuntimeRanges)
{
    soci::session session;
    orm::db::sqlite::SqliteBackend backend;
    const orm::SqliteConfig config{.database = orm::SqliteMemory{}};
    backend.runtime().open(session, orm::db::ConnectionConfigView{config});
    soci::values parameters;
    auto consumed = false;
    const auto consume = [&](const auto&) { consumed = true; };
    orm::db::binding::TypedResultStorage dates{{{"date", orm::model::ColumnType::Date}}};
    EXPECT_THROW(dates.read(session, "SELECT '0999-12-31'", parameters, consume, orm::db::BackendType::Mysql),
                 orm::db::binding::ConversionError);
    EXPECT_FALSE(consumed);

    const orm::db::BackendRuntimeLimits runtime{
        .maxUtcTimestamp = orm::detail::values::parseUtcTimestamp("2038-01-19 03:14:07.999999+00")};
    orm::db::binding::TypedResultStorage timestamps{{{"utc", orm::model::ColumnType::UtcTimestamp}}};
    const auto validate = [&](const orm::db::BoundValue& value)
    {
        if (!orm::db::binding::isWithinValueLimits(value, {}, runtime))
            throw orm::db::binding::ConversionError{"The result exceeds its effective runtime range"};
    };
    EXPECT_THROW(timestamps.read(session, "SELECT '2040-01-01 00:00:00.000001'", parameters, consume,
                                 orm::db::BackendType::Mariadb, validate),
                 orm::db::binding::ConversionError);
    EXPECT_FALSE(consumed);
}

TEST(StatementParameterBindingTest, explicitResultBuffersPreserveSignedSqlValuesForUnsignedRangeChecks)
{
    soci::session session;
    orm::db::sqlite::SqliteBackend backend;
    const orm::SqliteConfig config{.database = orm::SqliteMemory{}};
    backend.runtime().open(session, orm::db::ConnectionConfigView{config});
    orm::db::binding::TypedResultStorage storage{
        {{"number", orm::model::ColumnType::UnsignedLongLong}, {"date", orm::model::ColumnType::Date}}};
    soci::values parameters;
    EXPECT_THROW(storage.read(
                     session, "SELECT -1, '2026-10-25'", parameters, [](const auto& row)
                     { (void)orm::db::binding::getNumericValue<unsigned long long>(row, "number"); },
                     orm::db::BackendType::Postgres),
                 orm::db::binding::ConversionError);
}

TEST(StatementParameterBindingTest, nativeBoundValuesPassThroughExplicitResultDecodingAndValidation)
{
    using orm::db::binding::TypedResultStorage;
    using orm::query::QueryValue;
    const std::array<QueryValue, 5> inputs{
        QueryValue{orm::detail::values::parseDate("2026-10-25")},
        QueryValue{orm::detail::values::parseTimeOfDay("12:34:56.123456")},
        QueryValue{orm::detail::values::parseLocalDateTime("2026-10-25 12:34:56.000001")},
        QueryValue{orm::detail::values::parseUtcTimestamp("2026-10-25 13:34:56.123456+01:00")},
        QueryValue{orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff")}};
    const std::array<std::string_view, 5> aliases{"day", "clock", "civil", "instant", "identifier"};
    soci::values parameters;
    std::vector<orm::db::binding::ResultColumn> columns;
    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
        const auto& value = inputs[index];
        orm::db::binding::bindBoundValue(parameters, aliases[index],
                                         {.logicalType = value.getLogicalType(), .value = value.get()});
        columns.push_back({std::string{aliases[index]}, value.getLogicalType()});
    }
    EXPECT_EQ(parameters.get<std::string>("day"), "2026-10-25");
    EXPECT_EQ(parameters.get<std::string>("clock"), "12:34:56.123456");
    EXPECT_EQ(parameters.get<std::string>("civil"), "2026-10-25 12:34:56.000001");
    EXPECT_EQ(parameters.get<std::string>("instant"), "2026-10-25 12:34:56.123456+00:00");
    EXPECT_EQ(parameters.get<std::string>("identifier"), "00112233-4455-6677-8899-aabbccddeeff");

    soci::session session;
    const orm::db::sqlite::SqliteBackend backend;
    const orm::SqliteConfig config{.database = orm::SqliteMemory{}};
    backend.runtime().open(session, orm::db::ConnectionConfigView{config});
    TypedResultStorage storage{std::move(columns)};
    std::vector<QueryValue> validated;
    auto rows = 0;
    storage.read(
        session, "SELECT :day,:clock,:civil,:instant,:identifier", parameters,
        [&](const TypedResultStorage& row)
        {
            ++rows;
            EXPECT_EQ(orm::db::binding::getScalarFieldValue<orm::Date>(row, "day"),
                      std::get<orm::Date>(inputs[0].get()));
            EXPECT_EQ(orm::db::binding::getScalarFieldValue<orm::TimeOfDay>(row, "clock"),
                      std::get<orm::TimeOfDay>(inputs[1].get()));
            EXPECT_EQ(orm::db::binding::getScalarFieldValue<orm::LocalDateTime>(row, "civil"),
                      std::get<orm::LocalDateTime>(inputs[2].get()));
            EXPECT_EQ(orm::db::binding::getScalarFieldValue<orm::UtcTimestamp>(row, "instant"),
                      std::get<orm::UtcTimestamp>(inputs[3].get()));
            EXPECT_EQ(orm::db::binding::getScalarFieldValue<orm::Uuid>(row, "identifier"),
                      std::get<orm::Uuid>(inputs[4].get()));
            EXPECT_EQ(validated.size(), inputs.size());
        },
        std::nullopt, [&](const orm::db::BoundValue& value)
        { validated.push_back(QueryValue::fromStorage(value.logicalType, value.value.value())); });
    EXPECT_EQ(rows, 1);
    EXPECT_EQ(validated, std::vector<QueryValue>(inputs.begin(), inputs.end()));
}

TEST(StatementParameterBindingTest, explicitResultBuffersRetainPrimitiveStorageAndTypeProperties)
{
    using orm::db::binding::TypedResultStorage;
    using enum orm::model::ColumnType;
    TypedResultStorage storage{{{"boolean", Bool},
                                {"character", Char},
                                {"unsigned_character", UnsignedChar},
                                {"small", Short},
                                {"unsigned_small", UnsignedShort},
                                {"integer", Int},
                                {"unsigned_integer", UnsignedInt},
                                {"large", LongLong},
                                {"unsigned_large", UnsignedLongLong},
                                {"floating", Float},
                                {"precise", Double},
                                {"text", String},
                                {"day", Date}}};
    soci::session session;
    const orm::db::sqlite::SqliteBackend backend;
    const orm::SqliteConfig config{.database = orm::SqliteMemory{}};
    backend.runtime().open(session, orm::db::ConnectionConfigView{config});
    soci::values parameters;
    auto rows = 0;
    storage.read(session,
                 "SELECT 1,65,200,-123,321,654,4000,9223372036854775806,5000,1.25,2.5,"
                 "'wire-value','2026-10-25'",
                 parameters,
                 [&](const TypedResultStorage& row)
                 {
                     ++rows;
                     EXPECT_TRUE(orm::db::binding::getNumericValue<bool>(row, "boolean"));
                     EXPECT_EQ(orm::db::binding::getNumericValue<char>(row, "character"), 'A');
                     EXPECT_EQ(orm::db::binding::getNumericValue<unsigned char>(row, "unsigned_character"), 200);
                     EXPECT_EQ(orm::db::binding::getNumericValue<short>(row, "small"), -123);
                     EXPECT_EQ(orm::db::binding::getNumericValue<unsigned short>(row, "unsigned_small"), 321);
                     EXPECT_EQ(orm::db::binding::getNumericValue<int>(row, "integer"), 654);
                     EXPECT_EQ(orm::db::binding::getNumericValue<unsigned int>(row, "unsigned_integer"), 4000U);
                     EXPECT_EQ(orm::db::binding::getNumericValue<long long>(row, "large"), 9223372036854775806LL);
                     EXPECT_EQ(orm::db::binding::getNumericValue<unsigned long long>(row, "unsigned_large"), 5000ULL);
                     EXPECT_EQ(orm::db::binding::getNumericValue<float>(row, "floating"), 1.25F);
                     EXPECT_EQ(orm::db::binding::getNumericValue<double>(row, "precise"), 2.5);
                     EXPECT_EQ(row.get_properties("unsigned_integer").get_data_type(), soci::dt_unsigned_long_long);
                     EXPECT_EQ(row.get_properties("floating").get_data_type(), soci::dt_double);
                     EXPECT_EQ(row.get_properties("text").get_data_type(), soci::dt_string);
                     EXPECT_EQ(row.get<std::string>("text"), "wire-value");
                 });
    EXPECT_EQ(rows, 1);
}

TEST(StatementParameterBindingTest, explicitResultAccessRejectsMissingNullAndMismatchedStorage)
{
    using orm::db::binding::ConversionError;
    using orm::db::binding::TypedResultStorage;
    TypedResultStorage storage{{{"missing", orm::model::ColumnType::Int}, {"text", orm::model::ColumnType::String}}};
    soci::session session;
    const orm::db::sqlite::SqliteBackend backend;
    const orm::SqliteConfig config{.database = orm::SqliteMemory{}};
    backend.runtime().open(session, orm::db::ConnectionConfigView{config});
    soci::values parameters;
    auto rows = 0;
    storage.read(session, "SELECT NULL,'present'", parameters,
                 [&](const TypedResultStorage& row)
                 {
                     ++rows;
                     EXPECT_EQ(row.get_indicator("missing"), soci::i_null);
                     EXPECT_THROW((void)row.get<int>("missing"), ConversionError);
                     EXPECT_THROW((void)row.get<int>("text"), ConversionError);
                     EXPECT_THROW((void)row.get<std::string>("unknown"), ConversionError);
                     EXPECT_EQ(row.get<std::string>("text"), "present");
                 });
    EXPECT_EQ(rows, 1);
}

TEST(StatementParameterBindingTest, explicitResultMetadataRejectsInvalidTypeAndUnavailableRelatedModel)
{
    const std::vector<orm::db::binding::ResultColumn> invalidColumns{
        {"invalid", static_cast<orm::model::ColumnType>(999)}}; // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    EXPECT_THROW((void)orm::db::binding::TypedResultStorage{invalidColumns}, orm::db::binding::ConversionError);
    const std::array<orm::model::ColumnView, 1> columns{
        {{.fieldName = "relation", .name = "relation", .kind = orm::model::FieldKind::ToOne}}};
    const orm::model::ModelDataView modelData{
        .type = orm::model::typeId<int>(), .tableName = "missing_target", .columns = columns};
    const std::array<const orm::model::ModelDataView*, 1> models{&modelData};
    const orm::model::SchemaView schema{.models = models};
    EXPECT_THROW((void)orm::db::binding::modelResultColumns(schema.at(0), true), orm::db::binding::ConversionError);
}

TEST(StatementParameterBindingTest, malformedEncodedResultIsRejectedBeforeValidationAndConsumption)
{
    orm::db::binding::TypedResultStorage storage{{{"identifier", orm::model::ColumnType::Uuid}}};
    soci::session session;
    const orm::db::sqlite::SqliteBackend backend;
    const orm::SqliteConfig config{.database = orm::SqliteMemory{}};
    backend.runtime().open(session, orm::db::ConnectionConfigView{config});
    soci::values parameters;
    auto consumed = false;
    auto validated = false;
    EXPECT_THROW(storage.read(
                     session, "SELECT 'private-invalid-uuid'", parameters, [&](const auto&) { consumed = true; },
                     std::nullopt, [&](const orm::db::BoundValue&) { validated = true; }),
                 orm::db::binding::ConversionError);
    EXPECT_FALSE(consumed);
    EXPECT_FALSE(validated);
}

TEST(StatementParameterBindingTest, transportTruncationIsRejectedBeforeHydration)
{
    EXPECT_THROW(orm::db::binding::validateResultIndicator(soci::i_truncated), orm::db::binding::ConversionError);
}
