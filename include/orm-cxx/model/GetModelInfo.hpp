#pragma once

#include <optional>
#include <type_traits>
#include <typeindex>
#include <unordered_set>
#include <utility>

#include "AutoIncrementInfo.hpp"
#include "GetForeignModelInfoFromField.hpp"
#include "ModelInfo.hpp"
#include "RelationMetadata.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"

namespace orm::model
{
template <typename T>
auto getModelInfo() -> ModelInfo
{
    ModelInfo modelInfo;

    modelInfo.idColumnsNames = getPrimaryIdColumnsNames<T>();
    const auto autoIncrementColumnsNames = getAutoIncrementColumnsNames<T>();
    modelInfo.tableName = getTableName<T>();
    modelInfo.columnsInfo = getColumnsInfo<T>(modelInfo.idColumnsNames);

    using model_tuple_t = std::decay_t<decltype(rfl::to_view(std::declval<T&>()).values())>;
    auto fields = rfl::fields<T>();
    std::unordered_set<std::string> collectionFields;
    std::size_t columnIndex = 0;

    auto inspectField = [&modelInfo, &fields, &collectionFields, &columnIndex](auto i, auto field)
    {
        using field_t = std::decay_t<decltype(*field)>;
        const auto fieldName = std::string{fields[i].name()};

        if constexpr (is_optional_relation_collection_v<field_t>)
        {
            throw std::invalid_argument{"Collection relation field '" + fieldName + "' cannot be optional"};
        }
        else if constexpr (is_relation_collection_v<field_t>)
        {
            collectionFields.insert(fieldName);
            modelInfo.relationsInfo.push_back(detail::makeCollectionRelation<T, field_t>(fieldName));
        }
        else
        {
            GetForeignModelInfoFromField<T, field_t>::get(columnIndex, modelInfo);
            detail::appendToOneRelation<T, field_t>(fieldName, modelInfo.columnsInfo.at(columnIndex), modelInfo);
            ++columnIndex;
        }
    };

    utils::constexpr_for_tuple<model_tuple_t>(inspectField);

    detail::validateDescriptors<T>(collectionFields);

    applyAutoIncrementColumns(modelInfo, autoIncrementColumnsNames);
    detail::validateGlobalJunctionMappings(std::type_index{typeid(T)}, modelInfo);

    return modelInfo;
}

template <typename T>
auto getCachedModelInfo(bool force) -> ModelInfo&
{
    static std::optional<ModelInfo> modelInfo;
    if (not modelInfo.has_value() || force)
    {
        modelInfo = getModelInfo<T>();
    }
    return *modelInfo;
}
} // namespace orm::model
