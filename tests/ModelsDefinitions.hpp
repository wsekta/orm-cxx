#pragma once

#include <optional>
#include <string>

#include "orm-cxx/model/Mapping.hpp"
#include "orm-cxx/model/Schema.hpp"

namespace models
{
struct ModelWithOneField
{
    [[maybe_unused]] int field1;
};

struct ModelWithTableName
{
    [[maybe_unused]] int field1;

    inline static constexpr orm::reflection::FixedString table_name{"some_table_name"};
};

struct SomeDataModel
{
    int field1;
    std::string field2;
    [[maybe_unused]] double field3;
};

struct ModelWithOptional
{
    std::optional<int> field1;
    std::optional<std::string> field2;
    std::optional<double> field3;
};

struct ModelWithFloat
{
    int field1;
    std::string field2;
    [[maybe_unused]] float field3;
};

struct ModelWithOptionalFloat
{
    std::optional<int> field1;
    std::optional<std::string> field2;
    std::optional<float> field3;

    inline static constexpr orm::reflection::FixedString table_name{"models_ModelWithOptionalFloat"};
};

struct ModelWithId
{
    int id;
    [[maybe_unused]] int field1;
    std::string field2;
};

struct ModelWithAutoIncrementId
{
    int id;
    int field1;
    std::string field2;

    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&ModelWithAutoIncrementId::id>();
};

struct ModelWithAutoIncrementIdAndNamesMapping
{
    int id;
    int field1;
    std::string field2;

    inline static constexpr auto auto_increment_columns =
        orm::autoIncrement<&ModelWithAutoIncrementIdAndNamesMapping::id>();
    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&ModelWithAutoIncrementIdAndNamesMapping::field1, "some_field1_name">(),
                         orm::columnName<&ModelWithAutoIncrementIdAndNamesMapping::field2, "some_field2_name">(),
                         orm::columnName<&ModelWithAutoIncrementIdAndNamesMapping::id, "some_id_name">());
};

struct ModelWithOverwrittenId
{
    int id;
    [[maybe_unused]] int field1;
    std::string field2;

    inline static constexpr auto id_columns =
        orm::primaryKey<&ModelWithOverwrittenId::field1, &ModelWithOverwrittenId::field2>();
};

struct ModelWithIdAndNamesMapping
{
    int id;
    int field1;
    std::string field2;

    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&ModelWithIdAndNamesMapping::field1, "some_field1_name">(),
                         orm::columnName<&ModelWithIdAndNamesMapping::field2, "some_field2_name">(),
                         orm::columnName<&ModelWithIdAndNamesMapping::id, "some_id_name">());
};

struct ModelRelatedToOtherModel
{
    int id;
    [[maybe_unused]] int field1;
    std::string field2;
    ModelWithId field3;
};

struct ModelRelatedToAutoIncrementModel
{
    int id;
    int field1;
    ModelWithAutoIncrementId field3;
};

struct ModelRelatedToCompositeIdModel
{
    int id;
    std::string field1;
    ModelWithOverwrittenId field3;
};

struct ModelOptionallyRelatedToOtherModel
{
    int id;
    [[maybe_unused]] int field1;
    std::string field2;
    std::optional<ModelWithId> field3;
};

struct ModelWithAllBasicTypes
{
    int id;
    bool field1;
    char field2;
    unsigned char field3;
    short field4;
    unsigned short field5;
    int field6;
    unsigned int field7;
    long field8;
    unsigned long field9;
    long long field10;
    unsigned long long field11;
    float field12;
    double field13;
    std::string field14;

    inline static constexpr orm::reflection::FixedString table_name{"all_types"};
};

struct ModelWithAllInts
{
    int8_t field1;
    uint8_t field2;
    int16_t field3;
    uint16_t field4;
    int32_t field5;
    uint32_t field6;
    int64_t field7;
    uint64_t field8;
    size_t field9;

    inline static constexpr orm::reflection::FixedString table_name{"all_ints"};
};

struct ModelWithAutoIncrementNonPrimaryKey
{
    int id;
    int field1;

    inline static constexpr auto auto_increment_columns =
        orm::autoIncrement<&ModelWithAutoIncrementNonPrimaryKey::field1>();
};

struct ModelWithAutoIncrementCompositeId
{
    int id;
    int field1;

    inline static constexpr auto id_columns =
        orm::primaryKey<&ModelWithAutoIncrementCompositeId::id, &ModelWithAutoIncrementCompositeId::field1>();
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&ModelWithAutoIncrementCompositeId::id>();
};

struct ModelWithMultipleAutoIncrementColumns
{
    int id;
    int field1;

    inline static constexpr auto id_columns =
        orm::primaryKey<&ModelWithMultipleAutoIncrementColumns::id, &ModelWithMultipleAutoIncrementColumns::field1>();
    inline static constexpr auto auto_increment_columns =
        orm::autoIncrement<&ModelWithMultipleAutoIncrementColumns::id,
                           &ModelWithMultipleAutoIncrementColumns::field1>();
};

struct ModelWithAutoIncrementOptionalId
{
    std::optional<int> id;
    int field1;

    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&ModelWithAutoIncrementOptionalId::id>();
};

struct ModelWithAutoIncrementForeignModel
{
    int id;
    ModelWithId field3;

    inline static constexpr auto id_columns = orm::primaryKey<&ModelWithAutoIncrementForeignModel::field3>();
    inline static constexpr auto auto_increment_columns =
        orm::autoIncrement<&ModelWithAutoIncrementForeignModel::field3>();
};

struct ModelWithAutoIncrementWrongType
{
    int id;
    std::string field2;

    inline static constexpr auto id_columns = orm::primaryKey<&ModelWithAutoIncrementWrongType::field2>();
    inline static constexpr auto auto_increment_columns =
        orm::autoIncrement<&ModelWithAutoIncrementWrongType::field2>();
};

using Schema = orm::Schema<ModelWithOneField, ModelWithTableName, SomeDataModel, ModelWithOptional, ModelWithFloat,
                           ModelWithOptionalFloat, ModelWithId, ModelWithAutoIncrementId,
                           ModelWithAutoIncrementIdAndNamesMapping, ModelWithOverwrittenId, ModelWithIdAndNamesMapping,
                           ModelRelatedToOtherModel, ModelRelatedToAutoIncrementModel, ModelRelatedToCompositeIdModel,
                           ModelOptionallyRelatedToOtherModel, ModelWithAllBasicTypes, ModelWithAllInts>;
} // namespace models
