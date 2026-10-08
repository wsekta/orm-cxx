#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "orm-cxx/orm.hpp"

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
        orm::Database<AppSchema> database;
        database.connect("sqlite3://:memory:");
        database.createTable<User>();
        database.insert(std::vector<User>{{1, 18, "Ada", std::nullopt}, {2, 21, "Grace", "grace@example.test"}});

        for (const auto& user : database.select(adults, 18))
            std::cout << user.id << ": " << user.name << '\n';

        database.update(renameUser, std::string{"Ada Lovelace"}, 1);
        for (const auto& summary : database.select(userNamesPlan, 18))
            std::cout << summary.name << '\n';

        auto runtimeQuery = adults.toDynamic(18);
        runtimeQuery.andWhere(col<&User::name>().like("Ada%"));
        if (database.select(runtimeQuery).size() != 1 || database.remove(eraseUser, 1) != 1)
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
