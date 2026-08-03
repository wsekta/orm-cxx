#pragma once

#include <limits>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "orm-cxx/reflection/Reflection.hpp"

namespace orm
{
inline auto& testRng()
{
    static thread_local std::mt19937 gen{std::random_device{}()};
    return gen;
}

template <typename T>
auto fillField(T& field) -> void;

template <std::integral T>
auto fillField(T& field) -> void
{
    std::uniform_int_distribution<T> dist(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
    field = dist(testRng());
}

template <>
inline auto fillField<std::string>(std::string& field) -> void
{
    static constexpr const char* words[] = {
        "alpha",   "bravo",   "charlie", "delta",    "echo",     "foxtrot", "gamma",   "hotel",
        "india",   "juliet",  "kilo",    "lima",     "mike",     "november", "oscar",  "papa",
        "quebec",  "romeo",   "sierra",  "tango",    "uniform",  "victor",   "whiskey", "xray",
        "yankee",  "zulu"
    };
    static std::uniform_int_distribution<int> dist(0, 25);
    field = words[dist(testRng())];
}

template <std::floating_point T>
auto fillField(T& field) -> void
{
    std::uniform_real_distribution<T> dist(-1.0, 1.0);
    field = dist(testRng());
}

template <typename T>
auto fillField(std::optional<T>& field) -> void
{
    std::uniform_int_distribution<int> dist(0, 1);
    if (dist(testRng()) == 1)
    {
        field = T{};
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

    auto modelAsTuple = reflection::fieldPointers(model);

    auto fieldGenerator = [](auto, auto* field) { fillField(*field); };

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
