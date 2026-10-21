#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

import orm;

struct User
{
    int id;
    std::string name;
};

int main()
{
    using namespace orm::query;
    using AppSchema = orm::Schema<User>;

    orm::Database database{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = database.orm<AppSchema>();
    context.createTable<User>();

    context.insert(User{1, "Ada"});
    orm::Query<User> query;
    query.where(col<&User::id>() == 1);
    auto users = context.select(query);
    if (users.size() != 1 || users.front().name != "Ada")
        return 1;
    std::cout << "Selected: " << users.front().id << ' ' << users.front().name << '\n';

    orm::Update<User> update;
    update.set(col<&User::name>(), "Grace").where(col<&User::id>() == 1);
    const auto updated = context.update(update);
    users = context.select(query);
    if (updated != 1 || users.size() != 1 || users.front().name != "Grace")
        return 1;
    std::cout << "Updated: " << updated << " Grace\n";

    const auto removed = context.remove<User>(col<&User::id>() == 1);
    users = context.select(query);
    if (removed != 1 || !users.empty())
        return 1;
    std::cout << "Deleted: " << removed << "\nRemaining: " << users.size() << '\n';
}
