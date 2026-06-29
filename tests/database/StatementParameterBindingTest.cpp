#include "orm-cxx/database.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <vector>

#include "soci/values.h"
#include "tests/ModelsDefinitions.hpp"

namespace binding_test_models
{
struct ModelOptionallyRelatedToCompositeIdModel
{
    int id;
    std::optional<models::ModelWithOverwrittenId> field1;
};
} // namespace binding_test_models

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

TEST(StatementParameterBindingTest, shouldBindSupportedObjectFieldNullValueTypes)
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
        auto values = soci::values{};

        EXPECT_NO_THROW(orm::db::binding::setNullValue(values, "value", type));
    }
}

TEST(StatementParameterBindingTest, shouldRejectUnsupportedObjectFieldNullValueTypes)
{
    const auto unsupportedTypes = std::vector<orm::model::ColumnType>{
        orm::model::ColumnType::Uuid,
        orm::model::ColumnType::Unknown,
        orm::model::ColumnType::OneToOne,
        static_cast<orm::model::ColumnType>(999),
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

    EXPECT_NO_THROW(orm::detail::bindStatementParameter(values, parameter));
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
