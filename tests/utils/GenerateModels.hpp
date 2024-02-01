#pragma once

#include <vector>

#include "faker-cxx/Lorem.h"
#include "faker-cxx/Number.h"

namespace orm
{
template <typename T>
auto fillField(T& field) -> void
{
    field = T{};
}

template <std::integral T>
auto fillField(T& field) -> void
{
    field = faker::Number::integer<T>(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
}

template <>
inline auto fillField<std::string>(std::string& field) -> void
{
    field = faker::Lorem::word();
}

template <std::floating_point T>
auto fillField(T& field) -> void
{
    field = faker::Number::decimal<T>(-1.0, 1.0);
}

template <typename T>
auto fillField(std::optional<T>& field) -> void
{
    if (faker::Number::integer(0, 1) == 1)
    {
        field = T();
        fillField(*field);
    }
    else
    {
        field = std::nullopt;
    }
}

template <typename T>
auto generateModel() -> T
{
    T model;

    auto modelAsTuple = rfl::to_view(model).values();

    auto fieldGenerator = [](auto, auto& field) { fillField(field); };

    orm::utils::constexpr_for_tuple(modelAsTuple, fieldGenerator);

    return model;
}

template <typename T>
auto generateSomeDataModels(std::size_t count) -> std::vector<T>
{
    std::vector<T> result;

    for (std::size_t i = 0; i < count; i++)
    {
        result.emplace_back(generateModel<T>());
    }

    return result;
}
} // namespace orm
