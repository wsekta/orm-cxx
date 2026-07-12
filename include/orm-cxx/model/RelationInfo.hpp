#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <vector>

namespace orm::model
{
struct ModelInfo;

enum class RelationKind
{
    ToOne,
    OneToMany,
    ManyToMany,
};

struct JunctionInfo
{
    std::string tableName;
    std::vector<std::string> ownerColumns;
    std::vector<std::string> targetColumns;
    bool owningSide{};
};

struct RelationInfo
{
    std::string fieldName;
    std::string columnName;
    RelationKind kind{};
    std::string mappedBy;
    bool nullable{};
    std::optional<JunctionInfo> junction;
    std::type_index targetType{typeid(void)};
    std::function<const ModelInfo&()> targetModelResolver;

    [[nodiscard]] auto targetModel() const -> const ModelInfo&
    {
        if (not targetModelResolver)
        {
            throw std::logic_error{"Relation target model resolver is not configured"};
        }
        return targetModelResolver();
    }
};
} // namespace orm::model
