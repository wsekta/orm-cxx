#include "IdInfo.hpp"
#include "ModelInfo.hpp"

namespace orm::model
{
template <typename T>
auto getModelInfo() -> ModelInfo;

template <typename T, typename ModelField>
struct GetForeignModelInfoFromField
{
    static inline auto get(std::size_t i, ModelInfo& modelInfo) -> void
    {
        if constexpr (checkIfIsModelWithId<ModelField>())
        {
            auto name = rfl::fields<T>()[i].name();

            modelInfo.columnsInfo[i].isForeignModel = true;

            modelInfo.columnsInfo[i].type = ColumnType::OneToOne;

            modelInfo.foreignModelsInfo[name] = getModelInfo<ModelField>();
        }
    }
};

template <typename T, typename ModelField>
struct GetForeignModelInfoFromField<T, std::optional<ModelField>>
{
    static inline auto get(std::size_t i, ModelInfo& modelInfo) -> void
    {
        GetForeignModelInfoFromField<T, ModelField>::get(i, modelInfo);
    }
};
} // namespace orm::model
