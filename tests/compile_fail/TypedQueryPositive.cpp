#include <limits>
#include <type_traits>

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

static_assert(std::same_as<decltype(col<&User::age>())::Model, User>);
static_assert(std::same_as<decltype(col<&User::age>())::Value, int>);
static_assert(decltype(col<&User::profile, &Profile::id>())::nullable);
static_assert(detail::isSafeNumericWidening<double, float>);
static_assert(detail::isSafeNumericWidening<long long, int>);
static_assert(not detail::isSafeNumericWidening<float, double>);
static_assert(not detail::isSafeNumericWidening<int, unsigned int>);
static_assert(detail::isSafeNumericWidening<int, unsigned short>);
static_assert(detail::isSafeNumericWidening<double, int>);
static_assert(not detail::isSafeNumericWidening<float, int>);
static_assert(not detail::isSafeNumericWidening<int, bool>);
static_assert(not detail::compatibleValue<bool, int>);
static_assert(detail::compatibleValue<bool, bool>);
static_assert(detail::compatibleValue<std::string, const char*>);
static_assert(not detail::compatibleValue<int, std::optional<int>>);
static_assert(detail::isSafeNumericWidening<int, char16_t>);
static_assert(detail::isSafeNumericWidening<double, char32_t>);
static_assert(not detail::isSafeNumericWidening<short, char16_t>);
static_assert(not detail::isSafeNumericWidening<int, char32_t>);
static_assert(not detail::supportedScalar<long double>);
static_assert(not detail::supportedScalar<wchar_t>);
static_assert(not detail::supportedScalar<char16_t>);
static_assert(not detail::supportedScalar<char32_t>);
static_assert(not detail::isSafeNumericWidening<long double, double>);
static_assert(detail::isSafeNumericWidening<double, long double> ==
              (std::numeric_limits<double>::radix == std::numeric_limits<long double>::radix &&
               std::numeric_limits<double>::digits >= std::numeric_limits<long double>::digits &&
               std::numeric_limits<double>::max_exponent >= std::numeric_limits<long double>::max_exponent &&
               std::numeric_limits<double>::min_exponent <= std::numeric_limits<long double>::min_exponent));

template <typename Source>
auto instantiateFloatingValue() -> void
{
    if constexpr (detail::isSafeNumericWidening<double, Source>)
    {
        (void)(col<&User::score>() == Source{1.25});
        (void)(avg(col<&User::score>()) == Source{1.25});
        orm::Update<User> update;
        update.set(col<&User::score>(), Source{1.25});
    }
    else
    {
        static_assert(not requires(Source value) { col<&User::score>() == value; });
    }
}

using UserPredicate = decltype(col<&User::age>() == 1);
using RelatedPredicate = decltype(col<&User::profile, &Profile::city>() == "city");
using KeyPredicate = decltype(col<&User::profile, &Profile::id>() == 1);
using CollectionPredicate = decltype(exists<&User::roles>());
static_assert(std::same_as<typename decltype(!UserPredicate{col<&User::age>() == 1})::Model, User>);
static_assert(decltype((col<&User::age>() == 1) && (col<&User::id>() == 2))::writeSafe);
static_assert(not decltype((col<&User::age>() == 1) && (col<&User::profile, &Profile::city>() == "city"))::writeSafe);
static_assert(not decltype((col<&User::age>() == 1) || (col<&User::profile, &Profile::city>() == "city"))::writeSafe);
static_assert(not decltype(!(col<&User::profile, &Profile::city>() == "city"))::writeSafe);
static_assert(KeyPredicate::writeSafe);
static_assert(CollectionPredicate::writeSafe && CollectionPredicate::containsCollection);
static_assert(decltype(!exists<&User::roles>())::containsCollection);
static_assert(decltype((col<&User::id>() == 1) || exists<&User::roles>())::containsCollection);
static_assert(std::same_as<typename decltype(any<&User::roles>(col<&Role::id>() == 1))::Model, User>);
static_assert(std::same_as<typename decltype(countAll<User>())::Value, long long>);
static_assert(std::same_as<typename decltype(sum(col<&User::age>()))::Value, long long>);
static_assert(std::same_as<typename decltype(sum(col<&User::ratio>()))::Value, double>);
static_assert(std::same_as<typename decltype(avg(col<&User::age>()))::Value, double>);
static_assert(std::same_as<typename decltype(min(col<&User::name>()))::Value, std::string>);
static_assert(not decltype(countAll<User>())::nullable);
static_assert(decltype(sum(col<&User::age>()))::nullable);
static_assert(not std::is_default_constructible_v<decltype(col<&User::age>())>);
static_assert(not std::is_constructible_v<UserPredicate, detail::Predicate>);
static_assert(not std::is_constructible_v<TypedOrderBy<User>, detail::OrderBy>);
static_assert(not std::is_constructible_v<TypedProjection<User>, detail::Projection>);
static_assert(not std::is_constructible_v<TypedAggregate<User, long long, false>, detail::AggregateExpression>);
static_assert(not std::is_constructible_v<TypedAggregatePredicate<User>, detail::AggregatePredicate>);

[[maybe_unused]] auto instantiateTypedApi(Database& database) -> void
{
    instantiateFloatingValue<long double>();
    orm::Query<User> query;
    query.where(col<&User::age>() >= short{18})
        .andWhere(col<&User::name>().like("A%"))
        .orWhere(col<&User::email>() == nullptr)
        .orderBy(asc(col<&User::age>()), desc(col<&User::profile, &Profile::city>()))
        .groupBy(col<&User::name>())
        .having(countAll<User>() > 1)
        .andHaving(sum(col<&User::age>()) >= 2)
        .orHaving(avg(col<&User::age>()) > 18.0)
        .include<&User::roles>();
    (void)database.select(query);

    query
        .where((col<&User::age>().in(std::vector<short>{18, 21}) &&
                col<&User::active>().in(std::vector<bool>{true, false})) ||
               !(col<&User::name>().notIn({"Ada", "Grace"})))
        .andWhere(col<&User::age>().between(short{18}, 21))
        .orWhere(col<&User::age>().notBetween(18, short{21}))
        .andWhere(col<&User::unsignedAge>() == static_cast<unsigned short>(21))
        .andWhere(col<&User::score>() == 21)
        .andWhere(col<&User::ratio>() == short{21})
        .andWhere(col<&User::age>() == char16_t{21})
        .andWhere(col<&User::score>() == char32_t{21})
        .andWhere(col<&User::active>() == true)
        .andWhere(col<&User::active>().notIn(std::vector<bool>{false}))
        .andWhere(col<&User::email>() != std::nullopt)
        .orWhere(col<&User::email>().isNotNull())
        .having(!(countAll<User>() < 1) && (sum(col<&User::age>()) >= 18))
        .orHaving((min(col<&User::name>()) == "Ada") || (max(col<&User::age>()) == short{21}));
    (void)database.select(query);

    orm::ProjectionQuery<User, UserName> projection;
    projection.project(as("name", col<&User::name>())).where(raw<User>("1 = :value", param("value", 1)));
    projection.orderBy(rawOrder<User>("name ASC"));
    (void)database.select(projection);

    struct Stats
    {
        long long count;
        double average;
        std::optional<long long> total;
    };
    orm::ProjectionQuery<User, Stats> statistics;
    statistics
        .project(as("count", count(col<&User::name>())), as("average", avg(col<&User::age>())),
                 as("total", sum(col<&User::age>())))
        .where(col<&User::age>() >= short{18})
        .andWhere(col<&User::name>().notLike("Z%"))
        .orWhere(col<&User::email>().isNull())
        .groupBy(col<&User::name>())
        .having(countAll<User>() >= 1)
        .andHaving(sum(col<&User::age>()) > 1)
        .orHaving(max(col<&User::age>()) < 100)
        .orderBy(desc(col<&User::age>()));
    (void)database.select(statistics);

    orm::Update<User> update;
    update.set(col<&User::age>(), short{21})
        .set(col<&User::score>(), 0.5F)
        .set(col<&User::age>(), char16_t{21})
        .set(col<&User::score>(), char32_t{21})
        .set(col<&User::email>(), std::optional<std::string>{})
        .set(col<&User::email>(), std::optional<std::string>{"Ada"})
        .set(col<&User::email>(), std::nullopt)
        .set(col<&User::active>(), true)
        .set(col<&User::profile, &Profile::id>(), 1)
        .where((col<&User::profile, &Profile::id>() == 1) || !exists<&User::roles>())
        .andWhere(raw<User>("id > :id", param("id", 0)))
        .orWhere(col<&User::id>() == 1);
    (void)database.update(update);
    (void)database.remove<User>(col<&User::age>() < 18);
    (void)database.link<&User::roles>(User{}, Role{});
    (void)database.unlink<&User::roles>(User{}, Role{});

    orm::Query<Author> authors;
    authors.where(any<&Author::books>(col<&Book::title>().like("C++%")))
        .andWhere(none<&Author::books>(col<&Book::title>().like("Draft%")))
        .orWhere(exists<&Author::books>())
        .include<&Author::books>();
    (void)database.select(authors);
}
