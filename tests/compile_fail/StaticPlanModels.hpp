#pragma once

#include <optional>
#include <string>

namespace static_plan_models
{
struct Profile
{
    int id;
    std::string city;

    inline static constexpr orm::reflection::FixedString table_name{"static_plan_profiles"};
};

struct Role
{
    int id;
    std::string name;

    inline static constexpr orm::reflection::FixedString table_name{"static_plan_roles"};
};

struct User
{
    int id;
    int age;
    std::string name;
    std::optional<std::string> email;
    std::optional<Profile> profile;
    orm::ManyToMany<Role> roles;

    inline static constexpr orm::reflection::FixedString table_name{"static_plan_users"};
    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&User::roles>().through<"static_plan_user_roles">());
};

struct UserName
{
    std::string name;
};

struct UserStats
{
    std::optional<std::string> city;
    long long users;
    std::optional<long long> totalAge;
    std::optional<double> averageAge;
    std::optional<int> minimumAge;
    std::optional<int> maximumAge;
};

using Schema = orm::Schema<User, Profile, Role>;
using Context = orm::OrmContext<Schema>;
} // namespace static_plan_models
