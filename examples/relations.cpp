#include <cstddef>
#include <optional>
#include <string>

#include "orm-cxx/orm.hpp"

struct Book;

struct Author
{
    int id;
    std::string name;
    orm::OneToMany<Book> books;

    inline static const auto relations =
        orm::relations(orm::oneToMany("books").mappedBy("author"));
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

    inline static const auto relations =
        orm::relations(
            orm::manyToMany("roles")
                .through("user_roles")
                .ownerColumns({"user_id"})
                .targetColumns({"role_id"}));
};

struct Role
{
    int id;
    std::string name;
    orm::ManyToMany<User> users;

    inline static const auto relations =
        orm::relations(orm::manyToMany("users").mappedBy("roles"));
};

int main()
{
    using namespace orm::query;

    orm::Database database;
    database.connect("sqlite3://relations-example.db");

    // Junction tables are dropped before either endpoint table.
    database.deleteRelationTables<User>();
    database.deleteTable<Book>();
    database.deleteTable<Author>();
    database.deleteTable<Role>();
    database.deleteTable<User>();

    // Endpoint tables must exist before owning junction tables are created.
    database.createTable<Author>();
    database.createTable<Book>();
    database.createTable<User>();
    database.createTable<Role>();
    database.createRelationTables<User>();

    Author author{1, "Octavia Butler"};
    Book book{10, "Kindred", std::nullopt};
    User user{1, "Ada"};
    Role role{10, "admin"};

    database.insert(author);
    database.insert(book);
    database.insert(user);
    database.insert(role);

    const std::size_t assignedBooks = database.link(author, "books", book);
    const std::size_t assignedRoles = database.link(user, "roles", role);

    orm::Query<Author> authorQuery;
    authorQuery.include("books")
        .where(any("books", col("title").like("Kind%")));
    const auto authors = database.select(authorQuery);

    orm::Query<User> userQuery;
    userQuery.include("roles").where(exists("roles"));
    const auto users = database.select(userQuery);

    return assignedBooks == 1 && assignedRoles == 1 && !authors.empty() &&
                   authors.front().books.isLoaded() && !users.empty() &&
                   users.front().roles.isLoaded()
               ? 0
               : 1;
}
