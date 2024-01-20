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
    inline static const std::string table_name = "some_table_name";

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
    inline static const std::string table_name{"models_ModelWithFloat"};

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
} // namespace models
