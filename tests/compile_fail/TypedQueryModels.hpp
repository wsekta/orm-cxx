#pragma once

#include <optional>
#include <string>

namespace typed_query_models
{
struct Profile
{
    int id;
    std::string city;
};

struct Role;

struct User
{
    int id;
    int age;
    unsigned int unsignedAge;
    float ratio;
    double score;
    bool active;
    std::string name;
    std::optional<std::string> email;
    std::optional<Profile> profile;
    orm::ManyToMany<Role> roles;

    inline static constexpr int staticAge = 18;
    auto getAge() const -> int
    {
        return age;
    }

    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&User::roles>().through<"typed_user_roles">());
};

struct Role
{
    int id;
    std::string name;
    orm::ManyToMany<User> users;

    inline static constexpr auto relations = orm::relations(orm::manyToMany<&Role::users>().mappedBy<&User::roles>());
};

struct Book;

struct Author
{
    int id;
    std::string name;
    orm::OneToMany<Book> books;

    inline static constexpr auto relations = orm::relations(orm::oneToMany<&Author::books>().mappedBy<"author">());
};

struct Book
{
    int id;
    std::string title;
    std::optional<Author> author;
};

struct Other
{
    int id;
    int age;
};

struct UserName
{
    std::string name;
};

using Schema = orm::Schema<User, Profile, Role, Author, Book, Other>;
using Database = orm::Database<Schema>;
} // namespace typed_query_models
