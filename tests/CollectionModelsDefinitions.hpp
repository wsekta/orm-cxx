#pragma once

#include <optional>
#include <string>

#include "orm-cxx/model/Mapping.hpp"
#include "orm-cxx/model/Schema.hpp"
#include "orm-cxx/relations.hpp"

namespace collection_models
{
struct Book;

struct Author
{
    int id;
    std::string name;
    orm::OneToMany<Book> books;

    inline static constexpr orm::reflection::FixedString table_name{"collection_authors"};
    inline static constexpr auto relations = orm::relations(orm::oneToMany<&Author::books>().mappedBy<"author">());
};

struct Book
{
    int id;
    std::string title;
    std::optional<Author> author;

    inline static constexpr orm::reflection::FixedString table_name{"collection_books"};
};

struct RequiredBook;

struct RequiredAuthor
{
    int id;
    std::string name;
    orm::OneToMany<RequiredBook> books;

    inline static constexpr orm::reflection::FixedString table_name{"collection_required_authors"};
    inline static constexpr auto relations =
        orm::relations(orm::oneToMany<&RequiredAuthor::books>().mappedBy<"author">());
};

struct RequiredBook
{
    int id;
    std::string title;
    RequiredAuthor author;

    inline static constexpr orm::reflection::FixedString table_name{"collection_required_books"};
};

struct Role;

struct User
{
    int id;
    std::string name;
    orm::ManyToMany<Role> roles;

    inline static constexpr orm::reflection::FixedString table_name{"collection_users"};
    inline static constexpr auto relations = orm::relations(orm::manyToMany<&User::roles>()
                                                                .through<"collection_user_roles">()
                                                                .ownerColumns<"user_id">()
                                                                .targetColumns<"role_id">());
};

struct Role
{
    int id;
    std::string name;
    orm::ManyToMany<User> users;

    inline static constexpr orm::reflection::FixedString table_name{"collection_roles"};
    inline static constexpr auto relations = orm::relations(orm::manyToMany<&Role::users>().mappedBy<&User::roles>());
};

struct CompositeTag
{
    std::string scope;
    int id;
    std::string label;

    inline static constexpr orm::reflection::FixedString table_name{"collection_composite_tags"};
    inline static constexpr auto id_columns = orm::primaryKey<&CompositeTag::scope, &CompositeTag::id>();
};

struct CompositeOwner
{
    std::string tenant;
    int id;
    std::string name;
    orm::ManyToMany<CompositeTag> tags;

    inline static constexpr orm::reflection::FixedString table_name{"collection_composite_owners"};
    inline static constexpr auto id_columns = orm::primaryKey<&CompositeOwner::tenant, &CompositeOwner::id>();
    inline static constexpr auto relations = orm::relations(orm::manyToMany<&CompositeOwner::tags>()
                                                                .through<"collection_owner_tags">()
                                                                .ownerColumns<"owner_tenant", "owner_id">()
                                                                .targetColumns<"tag_scope", "tag_id">());
};

struct Permission
{
    int id;
    std::string name;

    inline static constexpr orm::reflection::FixedString table_name{"collection_permissions"};
};

struct Team
{
    int id;
    std::string name;

    inline static constexpr orm::reflection::FixedString table_name{"collection_teams"};
};

struct Member
{
    int id;
    std::string name;
    orm::ManyToMany<Permission> permissions;
    orm::ManyToMany<Team> teams;

    inline static constexpr orm::reflection::FixedString table_name{"collection_members"};
    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&Member::permissions>().through<"collection_member_permissions">(),
                       orm::manyToMany<&Member::teams>().through<"collection_member_teams">());
};

using Schema = orm::Schema<Author, Book, RequiredAuthor, RequiredBook, User, Role, CompositeTag, CompositeOwner,
                           Permission, Team, Member>;
} // namespace collection_models
