#include <array>
#include <type_traits>
#include <vector>

#include "orm-cxx/database.hpp"
#include "StaticPlanModels.hpp"

using namespace orm::query;
using namespace static_plan_models;

inline constexpr auto adults =
    select<User>().where(col<&User::age>() >= param<int, 0>()).orderBy(asc(col<&User::id>())).limit(10).offset(0);
inline constexpr auto userNamesPlan =
    selectAs<User, UserName>(as<"name">(col<&User::name>())).where(col<&User::age>() >= param<int, 0>());
inline constexpr auto changeAge =
    update<User>().set(col<&User::age>(), param<int, 0>()).where(col<&User::id>() == param<int, 1>());
inline constexpr auto eraseUser = remove<User>().where(col<&User::id>() == param<int, 0>());
inline constexpr auto repeatedSlot =
    select<User>().where((col<&User::age>() >= param<int, 0>()) && (col<&User::id>() != param<int, 0>()));
inline constexpr auto fixedIds = select<User>().where(col<&User::id>().in(std::array{1, 2, 3}));
inline constexpr auto variadicIds = select<User>().where(col<&User::id>().in(1, short{2}, 3));
inline constexpr auto relatedUsersPlan =
    select<User>().where((col<&User::profile, &Profile::id>() == 1) || exists<&User::roles>());
inline constexpr auto collected = select<User>().where(any<&User::roles>(col<&Role::id>() == param<int, 0>()));

using Predicate = decltype(col<&User::age>() >= 18);
using RelatedPredicate = decltype(col<&User::profile, &Profile::city>().isNull());
using CollectionPredicate = decltype(exists<&User::roles>());
static_assert(std::same_as<decltype(std::declval<Predicate>().dynamic()), TypedPredicate<User, true, false>>);
static_assert(std::same_as<decltype(std::declval<RelatedPredicate>().dynamic()), TypedPredicate<User, false, false>>);
static_assert(std::same_as<decltype(std::declval<CollectionPredicate>().dynamic()), TypedPredicate<User, true, true>>);
static_assert(std::same_as<decltype(asc(col<&User::id>()).dynamic()), TypedOrderBy<User>>);
static_assert(std::same_as<decltype(as<"name">(col<&User::name>()).dynamic()), TypedProjection<User>>);
static_assert(std::same_as<decltype(countAll<User>().dynamic()), TypedAggregate<User, long long, false>>);
static_assert(std::same_as<decltype(sum(col<&User::age>()).dynamic()), TypedAggregate<User, long long, true>>);
static_assert(std::same_as<decltype((countAll<User>() >= 1).dynamic()), TypedAggregatePredicate<User>>);

[[maybe_unused]] auto instantiateStaticApi(Database& database) -> void
{
    (void)database.select(adults, short{18});
    (void)database.select(userNamesPlan, 18);
    (void)database.select(repeatedSlot, 18);
    (void)database.select(fixedIds);
    (void)database.select(variadicIds);
    (void)database.select(relatedUsersPlan);
    (void)database.select(collected, 1);
    (void)database.select(select<User>().include<&User::roles>());
    (void)database.update(changeAge, 21, 1);
    (void)database.remove(eraseUser, 1);
    auto dynamicAdults = adults.toDynamic(18);
    auto dynamicNames = userNamesPlan.toDynamic(18);
    auto dynamicUpdate = changeAge.toDynamic(21, 1);
    (void)database.select(dynamicAdults);
    (void)database.select(dynamicNames);
    (void)database.update(dynamicUpdate);
    (void)database.remove<User>(eraseUser.toDynamic(1));

    const auto runtimeIds = select<User>().where(col<&User::id>().in(std::vector{1, 2}));
    (void)database.select(runtimeIds);
    const auto rawPlan = select<User>().where(raw<User>("age >= :age", param("age", 18)));
    (void)database.select(rawPlan);
    const auto listParameter = select<User>().where(col<&User::id>().in(param<std::vector<int>, 0>()));
    (void)database.select(listParameter, std::vector{1, 2});
    const auto arrayParameter = select<User>().where(col<&User::id>().in(param<std::array<int, 2>, 0>()));
    (void)database.select(arrayParameter, std::array{1, 2});

    const auto nullableUpdate = update<User>()
                                    .set(col<&User::email>(), param<std::optional<std::string>, 0>())
                                    .where(col<&User::id>() == param<int, 1>());
    (void)database.update(nullableUpdate, std::optional<std::string>{}, 1);
}
