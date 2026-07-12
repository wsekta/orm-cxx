#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "orm-cxx/relations.hpp"

namespace collection_models
{
struct Book;

struct Author
{
    inline static constexpr std::string_view table_name = "collection_authors";

    int id;
    std::string name;
    orm::OneToMany<Book> books;

    inline static const auto relations = orm::relations(orm::oneToMany("books").mappedBy("author"));
};

struct Book
{
    inline static constexpr std::string_view table_name = "collection_books";

    int id;
    std::string title;
    std::optional<Author> author;
};

struct RequiredBook;

struct RequiredAuthor
{
    inline static constexpr std::string_view table_name = "collection_required_authors";

    int id;
    std::string name;
    orm::OneToMany<RequiredBook> books;

    inline static const auto relations = orm::relations(orm::oneToMany("books").mappedBy("author"));
};

struct RequiredBook
{
    inline static constexpr std::string_view table_name = "collection_required_books";

    int id;
    std::string title;
    RequiredAuthor author;
};

struct Role;

struct User
{
    inline static constexpr std::string_view table_name = "collection_users";

    int id;
    std::string name;
    orm::ManyToMany<Role> roles;

    inline static const auto relations = orm::relations(
        orm::manyToMany("roles").through("collection_user_roles").ownerColumns({"user_id"}).targetColumns({"role_id"}));
};

struct Role
{
    inline static constexpr std::string_view table_name = "collection_roles";

    int id;
    std::string name;
    orm::ManyToMany<User> users;

    inline static const auto relations = orm::relations(orm::manyToMany("users").mappedBy("roles"));
};

struct CompositeTag
{
    inline static constexpr std::string_view table_name = "collection_composite_tags";
    inline static const std::vector<std::string> id_columns = {"scope", "id"};

    std::string scope;
    int id;
    std::string label;
};

struct CompositeOwner
{
    inline static constexpr std::string_view table_name = "collection_composite_owners";
    inline static const std::vector<std::string> id_columns = {"tenant", "id"};

    std::string tenant;
    int id;
    std::string name;
    orm::ManyToMany<CompositeTag> tags;

    inline static const auto relations = orm::relations(orm::manyToMany("tags")
                                                            .through("collection_owner_tags")
                                                            .ownerColumns({"owner_tenant", "owner_id"})
                                                            .targetColumns({"tag_scope", "tag_id"}));
};

struct Permission
{
    inline static constexpr std::string_view table_name = "collection_permissions";

    int id;
    std::string name;
};

struct Team
{
    inline static constexpr std::string_view table_name = "collection_teams";

    int id;
    std::string name;
};

struct Member
{
    inline static constexpr std::string_view table_name = "collection_members";

    int id;
    std::string name;
    orm::ManyToMany<Permission> permissions;
    orm::ManyToMany<Team> teams;

    inline static const auto relations =
        orm::relations(orm::manyToMany("permissions").through("collection_member_permissions"),
                       orm::manyToMany("teams").through("collection_member_teams"));
};
} // namespace collection_models
