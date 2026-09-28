#pragma once

#include <array>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "orm-cxx/reflection/detail/GeneratedBindings.hpp"
#include "tests/reflection/GeneratedFieldLimitModels.hpp"

namespace reflection_binding_tests
{
template <std::size_t Count>
void verifyGeneratedBinding()
{
    using Model = reflection_limit_models::RuntimeFields<Count>;
    using Arity = std::integral_constant<std::size_t, Count>;
    Model model{};

    using MutableBinding = decltype(orm::reflection::detail::bindingTraitsImpl(model, Arity{}));
    using ConstBinding = decltype(orm::reflection::detail::bindingTraitsImpl(std::as_const(model), Arity{}));
    static_assert(MutableBinding::fieldsAreAddressable);
    static_assert(ConstBinding::fieldsAreAddressable);
    static_assert(std::tuple_size_v<typename MutableBinding::Tuple> == Count);
    static_assert(std::tuple_size_v<typename ConstBinding::Tuple> == Count);
    static_assert(
        []<std::size_t... Indices>(std::index_sequence<Indices...>) {
            return (std::is_same_v<std::tuple_element_t<Indices, typename MutableBinding::Tuple>, int> && ...);
        }(std::make_index_sequence<Count>{}));
    static_assert(
        []<std::size_t... Indices>(std::index_sequence<Indices...>) {
            return (std::is_same_v<std::tuple_element_t<Indices, typename ConstBinding::Tuple>, const int> && ...);
        }(std::make_index_sequence<Count>{}));

    using MutableFunction = MutableBinding (*)(Model&, Arity) noexcept;
    using ConstFunction = ConstBinding (*)(const Model&, Arity) noexcept;
    MutableFunction volatile mutableFunction =
        static_cast<MutableFunction>(&orm::reflection::detail::bindingTraitsImpl<Model>);
    ConstFunction volatile constFunction =
        static_cast<ConstFunction>(&orm::reflection::detail::bindingTraitsImpl<const Model>);
    const auto mutableTraits = mutableFunction(model, Arity{});
    const auto constTraits = constFunction(std::as_const(model), Arity{});
    EXPECT_TRUE(mutableTraits.fieldsAreAddressable);
    EXPECT_TRUE(constTraits.fieldsAreAddressable);

    const auto expected = model.addresses();
    auto tied = [&]
    {
        if constexpr (Count == 0)
        {
            using TieFunction = std::tuple<> (*)(Model&, Arity) noexcept;
            TieFunction volatile tieFunction =
                static_cast<TieFunction>(&orm::reflection::detail::tieFieldsImpl<Model>);
            return tieFunction(model, Arity{});
        }
        else
        {
            return orm::reflection::detail::tieFieldsImpl(model, Arity{});
        }
    }();
    const auto actual = std::apply([](auto&... fields)
                                   { return std::array<int*, sizeof...(fields)>{std::addressof(fields)...}; }, tied);
    for (std::size_t index = 0; index < Count; ++index)
    {
        EXPECT_EQ(actual[index], expected[index]);
        EXPECT_EQ(*actual[index], static_cast<int>(index));
        *actual[index] += 1000;
    }

    auto constTied = orm::reflection::detail::tieFieldsImpl(std::as_const(model), Arity{});
    const auto constActual =
        std::apply([](const auto&... fields)
                   { return std::array<const int*, sizeof...(fields)>{std::addressof(fields)...}; }, constTied);
    for (std::size_t index = 0; index < Count; ++index)
    {
        EXPECT_EQ(constActual[index], expected[index]);
        EXPECT_EQ(*constActual[index], static_cast<int>(index + 1000));
    }
}

template <std::size_t Begin, std::size_t... Offsets>
void verifyRange(std::index_sequence<Offsets...>)
{
    (verifyGeneratedBinding<Begin + Offsets>(), ...);
}
} // namespace reflection_binding_tests
