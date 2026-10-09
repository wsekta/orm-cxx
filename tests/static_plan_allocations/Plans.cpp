module;

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>

module orm;

import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;

namespace
{
struct User
{
    int id;
    int age;
    std::string name;
    std::optional<std::string> email;
};

struct Summary
{
    std::string name;
};

template <typename T>
auto stateChecksum(const T& value) -> std::size_t
{
    namespace detail = orm::query::detail;
    if constexpr (detail::isStaticPlan<T>)
        return stateChecksum(detail::PlanAccess::get(value));
    else if constexpr (detail::isExpression<T>)
        return stateChecksum(detail::TypedAccess::children(value));
    else if constexpr (requires { std::tuple_size<T>::value; })
        return std::apply([](const auto&... element) { return (std::size_t{} + ... + stateChecksum(element)); }, value);
    else if constexpr (requires { value.values; })
        return stateChecksum(value.values);
    else if constexpr (requires { value.value; })
        return stateChecksum(value.value);
    else if constexpr (requires { value.view(); })
    {
        std::size_t checksum = 0;
        for (const auto character : value.view())
            checksum += static_cast<unsigned char>(character);
        return checksum;
    }
    else if constexpr (std::is_arithmetic_v<T>)
        return static_cast<std::size_t>(value);
    else if constexpr (detail::isParameter<T>)
        return T::index + 1;
    else
        return 1;
}

auto buildAndCopyPlans(int age) -> std::size_t
{
    using namespace orm::query;
    const auto selectPlan =
        select<User>()
            .where((col<&User::age>() >= param<int, 0>()) &&
                   (col<&User::name>() == "A fixed string longer than a typical small-string buffer"))
            .andWhere(col<&User::id>().in(std::array{1, 2, 3}))
            .orderBy(asc(col<&User::id>()))
            .limit(10);
    const auto projected =
        selectAs<User, Summary>(as<"name">(col<&User::name>())).where(col<&User::age>().between(age, age + 10));
    const auto updatePlan = update<User>()
                                .set(col<&User::age>(), age)
                                .set(col<&User::name>(), "Another fixed string longer than a small-string buffer")
                                .where(col<&User::id>() == param<int, 0>());
    const auto removePlan = remove<User>().where(col<&User::age>() < age);
    const auto selectedCopy = selectPlan;
    const auto projectedCopy = projected;
    const auto updatedCopy = updatePlan;
    const auto removedCopy = removePlan;
    return stateChecksum(selectedCopy) + stateChecksum(projectedCopy) + stateChecksum(updatedCopy) +
           stateChecksum(removedCopy);
}
} // namespace

// C linkage keeps the executable's allocation hooks and main in the global
// module while the measured plan code can inspect private orm declarations.
extern "C" auto orm_cxx_build_copy_plans(int age) -> std::size_t
{
    return buildAndCopyPlans(age);
}
