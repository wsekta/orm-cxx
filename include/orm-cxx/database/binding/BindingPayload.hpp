#pragma once

#include <cstddef>
#include <stdexcept>
#include <type_traits>

#include "orm-cxx/model/Schema.hpp"

namespace orm::db::binding
{
namespace detail
{
template <typename T, typename SchemaType, std::size_t FieldIndex>
[[nodiscard]] consteval auto mappedColumnPointer() noexcept -> const model::ColumnView*
{
    constexpr auto descriptor = model::modelView<SchemaType, T>();
    for (const auto& column : descriptor->columns)
    {
        if (column.fieldIndex == FieldIndex)
        {
            return &column;
        }
    }
    return nullptr;
}

template <typename T, typename SchemaType, std::size_t FieldIndex>
inline constexpr auto mappedColumn = mappedColumnPointer<T, SchemaType, FieldIndex>();

template <typename T, typename SchemaType, std::size_t FieldIndex>
[[nodiscard]] constexpr auto columnForField() noexcept -> const model::ColumnView&
{
    static_assert(mappedColumn<T, SchemaType, FieldIndex> != nullptr,
                  "A bound field must have a column descriptor in the selected schema");
    return *mappedColumn<T, SchemaType, FieldIndex>;
}

template <typename T, typename SchemaType>
[[nodiscard]] constexpr auto columnForField(std::size_t fieldIndex) -> const model::ColumnView&
{
    constexpr auto descriptor = model::modelView<SchemaType, T>();
    for (const auto& column : descriptor->columns)
    {
        if (column.fieldIndex == fieldIndex)
        {
            return column;
        }
    }
    throw std::invalid_argument{"A bound field has no column descriptor in the selected schema"};
}
} // namespace detail

template <typename T, typename SchemaType, bool JoinedValues = false>
struct BindingPayload
{
    static_assert(SchemaType::template contains<std::remove_cv_t<T>>, "BindingPayload model must belong to its Schema");

    mutable T value{};
    inline static constexpr bool joinedValues = JoinedValues;

    [[nodiscard]] static constexpr auto modelDescriptor() noexcept -> model::ModelView
    {
        return model::modelView<SchemaType, std::remove_cv_t<T>>();
    }

    template <std::size_t FieldIndex>
    [[nodiscard]] static constexpr auto columnDescriptor() noexcept -> const model::ColumnView&
    {
        return detail::columnForField<std::remove_cv_t<T>, SchemaType, FieldIndex>();
    }

    [[nodiscard]] static constexpr auto columnDescriptor(std::size_t fieldIndex) -> const model::ColumnView&
    {
        return detail::columnForField<std::remove_cv_t<T>, SchemaType>(fieldIndex);
    }
};
} // namespace orm::db::binding
