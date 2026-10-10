#include <cstddef>
#include <optional>
#include <string>

import orm;

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

struct Role;

struct User
{
    int id;
    std::string name;
    orm::ManyToMany<Role> roles;

    inline static constexpr auto relations = orm::relations(
        orm::manyToMany<&User::roles>().through<"user_roles">().ownerColumns<"user_id">().targetColumns<"role_id">());
};

struct Role
{
    int id;
    std::string name;
    orm::ManyToMany<User> users;

    inline static constexpr auto relations = orm::relations(orm::manyToMany<&Role::users>().mappedBy<&User::roles>());
};

int main() // NOLINT(bugprone-exception-escape)
{
    using namespace orm::query;

    using AppSchema = orm::Schema<Author, Book, User, Role>;
    orm::Database database;
    auto context = database.orm<AppSchema>();
    database.connect("sqlite3://relations-example.db");

    // Junction tables are dropped before either endpoint table.
    context.deleteRelationTables<User>();
    context.deleteTable<Book>();
    context.deleteTable<Author>();
    context.deleteTable<Role>();
    context.deleteTable<User>();

    // Endpoint tables must exist before owning junction tables are created.
    context.createTable<Author>();
    context.createTable<Book>();
    context.createTable<User>();
    context.createTable<Role>();
    context.createRelationTables<User>();

    Author author{1, "Octavia Butler", {}};
    Book book{10, "Kindred", std::nullopt};
    User user{1, "Ada", {}};
    Role role{10, "admin", {}};

    context.insert(author);
    context.insert(book);
    context.insert(user);
    context.insert(role);

    const std::size_t assignedBooks = context.link<&Author::books>(author, book);
    const std::size_t assignedRoles = context.link<&User::roles>(user, role);

    orm::Query<Author> authorQuery;
    authorQuery.include<&Author::books>().where(any<&Author::books>(col<&Book::title>().like("Kind%")));
    const auto authors = context.select(authorQuery);

    orm::Query<User> userQuery;
    userQuery.include<&User::roles>().where(exists<&User::roles>());
    const auto users = context.select(userQuery);

    return assignedBooks == 1 && assignedRoles == 1 && !authors.empty() && authors.front().books.isLoaded() &&
                   !users.empty() && users.front().roles.isLoaded() ?
               0 :
               1;
}
