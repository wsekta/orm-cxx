#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace models
{
struct ModelWithOneField
{
    [[maybe_unused]] int field1;
};

struct ModelWithTableName
{
    inline static constexpr std::string_view table_name = "some_table_name";

    [[maybe_unused]] int field1;
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
    inline static constexpr std::string_view table_name{"models_ModelWithFloat"};

    std::optional<int> field1;
    std::optional<std::string> field2;
    std::optional<float> field3;
};

struct ModelWithId
{
    int id;
    [[maybe_unused]] int field1;
    std::string field2;
};

struct ModelWithOverwrittenId
{
    int id;
    [[maybe_unused]] int field1;
    std::string field2;

    inline static const std::vector<std::string> id_columns = {"field1", "field2"};
};

struct ModelWithIdAndNamesMapping
{
    int id;
    int field1;
    std::string field2;

    inline static const std::map<std::string, std::string> columns_names = {
        {"field1", "some_field1_name"}, {"field2", "some_field2_name"}, {"id", "some_id_name"}};
};

struct ModelRelatedToOtherModel
{
    int id;
    [[maybe_unused]] int field1;
    std::string field2;
    ModelWithId field3;
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
    inline static constexpr std::string_view table_name = "all_types";

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
};

struct ModelWithAllInts
{
    inline static constexpr std::string_view table_name = "all_ints";

    int8_t field1;
    uint8_t field2;
    int16_t field3;
    uint16_t field4;
    int32_t field5;
    uint32_t field6;
    int64_t field7;
    uint64_t field8;
    size_t field9;
};
} // namespace models
