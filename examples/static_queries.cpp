#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

import orm;

struct User
{
    int id;
    int age;
    std::string name;
    std::optional<std::string> email;
};

struct UserName
{
    std::string name;
};

using AppSchema = orm::Schema<User>;
using namespace orm::query;

inline constexpr auto adults =
    select<User>().where(col<&User::age>() >= param<int, 0>()).orderBy(asc(col<&User::id>()));
inline constexpr auto userNamesPlan =
    selectAs<User, UserName>(as<"name">(col<&User::name>())).where(col<&User::age>() >= param<int, 0>());
inline constexpr auto renameUser =
    update<User>().set(col<&User::name>(), param<std::string, 0>()).where(col<&User::id>() == param<int, 1>());
inline constexpr auto eraseUser = remove<User>().where(col<&User::id>() == param<int, 0>());

int main()
{
    try
    {
        orm::Database database;
        auto context = database.orm<AppSchema>();
        database.connect("sqlite3://:memory:");
        context.createTable<User>();
        context.insert(std::vector<User>{{1, 18, "Ada", std::nullopt}, {2, 21, "Grace", "grace@example.test"}});

        for (const auto& user : context.select(adults, 18))
            std::cout << user.id << ": " << user.name << '\n';

        context.update(renameUser, std::string{"Ada Lovelace"}, 1);
        for (const auto& summary : context.select(userNamesPlan, 18))
            std::cout << summary.name << '\n';

        auto runtimeQuery = adults.toDynamic(18);
        runtimeQuery.andWhere(col<&User::name>().like("Ada%"));
        if (context.select(runtimeQuery).size() != 1 || context.remove(eraseUser, 1) != 1)
            return 1;

        database.disconnect();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
