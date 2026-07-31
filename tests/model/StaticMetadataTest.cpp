#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <type_traits>

#include "orm-cxx/model/Schema.hpp"
#include "orm-cxx/relations.hpp"

namespace static_metadata_models
{
struct DefaultNames
{
    int value;
};

struct TypedMapping
{
    int id;
    std::optional<std::string> displayName;
    double score;

    inline static constexpr orm::reflection::FixedString table_name{"typed_mappings"};
    inline static constexpr auto columns_names = orm::columnNames(
        orm::columnName<&TypedMapping::id, "user_id">(), orm::columnName<&TypedMapping::displayName, "display_name">());
    inline static constexpr auto id_columns = orm::primaryKey<&TypedMapping::id>();
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&TypedMapping::id>();
};

struct DefaultId
{
    int id;
    std::string value;
};

struct CompositeId
{
    int tenant;
    std::string key;
    std::optional<int> value;

    inline static constexpr auto id_columns = orm::primaryKey<&CompositeId::key, &CompositeId::tenant>();
};

struct ExplicitlyKeyless
{
    int id;

    inline static constexpr auto id_columns = orm::primaryKey<>();
};

struct Parent
{
    int id;
};

struct Child
{
    int id;
    std::optional<Parent> parent;
};

struct RequiredChild
{
    int id;
    Parent parent;
};

struct Book;

struct Author
{
    int id;
    orm::OneToMany<Book> books;

    inline static constexpr auto relations = orm::relations(orm::oneToMany<&Author::books>().mappedBy<"author">());
};

struct Book
{
    int id;
    std::optional<Author> author;
};

struct Role;

struct User
{
    int id;
    orm::ManyToMany<Role> roles;

    inline static constexpr auto relations = orm::relations(
        orm::manyToMany<&User::roles>().through<"user_roles">().ownerColumns<"user_id">().targetColumns<"role_id">());
};

struct Role
{
    int id;
    orm::ManyToMany<User> users;

    inline static constexpr auto relations = orm::relations(orm::manyToMany<&Role::users>().mappedBy<&User::roles>());
};

struct DefaultTarget
{
    int id;

    inline static constexpr orm::reflection::FixedString table_name{"default_targets"};
};

struct DefaultOwner
{
    int id;
    orm::ManyToMany<DefaultTarget> targets;

    inline static constexpr orm::reflection::FixedString table_name{"default_owners"};
    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&DefaultOwner::targets>().through<"default_owner_targets">());
};

struct SelfRelated
{
    int id;
    orm::ManyToMany<SelfRelated> peers;

    inline static constexpr orm::reflection::FixedString table_name{"self_related"};
    inline static constexpr auto relations = orm::relations(orm::manyToMany<&SelfRelated::peers>()
                                                                .through<"self_links">()
                                                                .ownerColumns<"source_id">()
                                                                .targetColumns<"target_id">());
};

struct LookupParent
{
    int id;
};

struct LookupChild;

struct RelationLookupCollision
{
    int id;
    std::optional<LookupParent> parent;
    orm::OneToMany<LookupChild> children;

    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&RelationLookupCollision::parent, "children">());
    inline static constexpr auto relations =
        orm::relations(orm::oneToMany<&RelationLookupCollision::children>().mappedBy<"owner">());
};

struct LookupChild
{
    int id;
    std::optional<RelationLookupCollision> owner;
};
} // namespace static_metadata_models

namespace
{
using static_metadata_models::Author;
using static_metadata_models::Book;
using static_metadata_models::Child;
using static_metadata_models::CompositeId;
using static_metadata_models::DefaultId;
using static_metadata_models::DefaultNames;
using static_metadata_models::DefaultOwner;
using static_metadata_models::DefaultTarget;
using static_metadata_models::ExplicitlyKeyless;
using static_metadata_models::LookupChild;
using static_metadata_models::LookupParent;
using static_metadata_models::Parent;
using static_metadata_models::RelationLookupCollision;
using static_metadata_models::RequiredChild;
using static_metadata_models::Role;
using static_metadata_models::SelfRelated;
using static_metadata_models::TypedMapping;
using static_metadata_models::User;

using TestSchema = orm::Schema<DefaultNames, TypedMapping, DefaultId, CompositeId, ExplicitlyKeyless, Parent, Child,
                               RequiredChild, Author, Book, User, Role, DefaultTarget, DefaultOwner, SelfRelated,
                               LookupParent, RelationLookupCollision, LookupChild>;

template <typename Model>
inline constexpr auto metadata = orm::modelView<TestSchema, Model>();

static_assert(std::is_trivially_copyable_v<orm::model::ColumnView>);
static_assert(std::is_trivially_copyable_v<orm::model::RelationView>);
static_assert(std::is_trivially_copyable_v<orm::model::ModelView>);

static_assert(orm::model::logicalType<int>() == orm::model::ColumnType::Int);
static_assert(orm::model::logicalType<std::optional<std::string>>() == orm::model::ColumnType::String);
static_assert(orm::model::logicalType<double>() == orm::model::ColumnType::Double);
static_assert(not orm::model::isNullable<double>);
static_assert(orm::model::isNullable<std::optional<double>>);

static_assert(metadata<DefaultNames>->columns.size() == 1);
static_assert(metadata<DefaultNames>->columns[0].fieldName == "value");
static_assert(metadata<DefaultNames>->columns[0].name == "value");
static_assert(metadata<DefaultNames>.primaryKeySize() == 0);
static_assert(metadata<DefaultNames>->tableName.ends_with("static_metadata_models_DefaultNames"));

static_assert(metadata<TypedMapping>->tableName == "typed_mappings");
static_assert(metadata<TypedMapping>->columns.size() == 3);
static_assert(metadata<TypedMapping>->columns[0].fieldName == "id");
static_assert(metadata<TypedMapping>->columns[0].name == "user_id");
static_assert(metadata<TypedMapping>->columns[0].isPrimaryKey);
static_assert(metadata<TypedMapping>->columns[0].isAutoIncrement);
static_assert(metadata<TypedMapping>->columns[0].isNotNull);
static_assert(metadata<TypedMapping>->columns[1].fieldName == "displayName");
static_assert(metadata<TypedMapping>->columns[1].name == "display_name");
static_assert(metadata<TypedMapping>->columns[1].type == orm::model::ColumnType::String);
static_assert(not metadata<TypedMapping>->columns[1].isNotNull);
static_assert(metadata<TypedMapping>->columns[2].name == "score");
static_assert(metadata<TypedMapping>.findColumn("displayName") == metadata<TypedMapping>.findColumn("display_name"));
static_assert(metadata<TypedMapping>.findColumn("missing") == nullptr);

static_assert(metadata<DefaultId>.primaryKeySize() == 1);
static_assert(metadata<DefaultId>->primaryKeyIndices[0] == 0);
static_assert(metadata<DefaultId>->columns[0].isPrimaryKey);
static_assert(not metadata<DefaultId>->columns[1].isPrimaryKey);

static_assert(metadata<CompositeId>.primaryKeySize() == 2);
static_assert(metadata<CompositeId>->primaryKeyIndices[0] == 1);
static_assert(metadata<CompositeId>->primaryKeyIndices[1] == 0);
static_assert(not metadata<CompositeId>->columns[2].isPrimaryKey);
static_assert(metadata<ExplicitlyKeyless>.primaryKeySize() == 0);
static_assert(not metadata<ExplicitlyKeyless>->columns[0].isPrimaryKey);

static_assert(metadata<Child>->columns[1].kind == orm::model::FieldKind::ToOne);
static_assert(metadata<Child>->columns[1].targetModelIndex == TestSchema::indexOf<Parent>());
static_assert(not metadata<Child>->columns[1].isNotNull);
static_assert(metadata<Child>->relations[0].kind == orm::model::RelationKind::ToOne);
static_assert(metadata<Child>->relations[0].nullable);
static_assert(metadata<Child>.resolveTarget(metadata<Child>->columns[1]) == metadata<Parent>);
static_assert(metadata<Child>.resolveTarget(metadata<Child>->relations[0]) == metadata<Parent>);
static_assert(metadata<RequiredChild>->columns[1].isNotNull);
static_assert(not metadata<RequiredChild>->relations[0].nullable);

static_assert(metadata<Author>->columns.size() == 1);
static_assert(metadata<Author>->relations.size() == 1);
static_assert(metadata<Author>->relations[0].fieldName == "books");
static_assert(metadata<Author>->relations[0].kind == orm::model::RelationKind::OneToMany);
static_assert(metadata<Author>->relations[0].mappedBy == "author");
static_assert(metadata<Author>.resolveTarget(metadata<Author>->relations[0]) == metadata<Book>);
static_assert(metadata<Book>.findRelation("author") != nullptr);

static_assert(metadata<User>->relations[0].kind == orm::model::RelationKind::ManyToMany);
static_assert(metadata<User>->relations[0].junction.tableName == "user_roles");
static_assert(metadata<User>->relations[0].junction.ownerColumns[0] == "user_id");
static_assert(metadata<User>->relations[0].junction.targetColumns[0] == "role_id");
static_assert(metadata<User>->relations[0].junction.owningSide);
static_assert(metadata<User>.resolveTarget(metadata<User>->relations[0]) == metadata<Role>);

static_assert(metadata<Role>->relations[0].mappedBy == "roles");
static_assert(not metadata<Role>->relations[0].junction.isConfigured());
static_assert(metadata<Role>.resolveTarget(metadata<Role>->relations[0]) == metadata<User>);
static_assert(metadata<Role>.resolveJunction(metadata<Role>->relations[0]).tableName == "user_roles");
static_assert(metadata<Role>.resolveJunction(metadata<Role>->relations[0]).ownerColumns[0] == "role_id");
static_assert(metadata<Role>.resolveJunction(metadata<Role>->relations[0]).targetColumns[0] == "user_id");
static_assert(not metadata<Role>.resolveJunction(metadata<Role>->relations[0]).owningSide);

static_assert(metadata<DefaultOwner>->relations[0].junction.ownerColumns[0] == "default_owners_id");
static_assert(metadata<DefaultOwner>->relations[0].junction.targetColumns[0] == "default_targets_id");

static_assert(metadata<SelfRelated>->relations[0].junction.tableName == "self_links");
static_assert(metadata<SelfRelated>->relations[0].junction.ownerColumns[0] == "source_id");
static_assert(metadata<SelfRelated>->relations[0].junction.targetColumns[0] == "target_id");
static_assert(metadata<SelfRelated>.resolveTarget(metadata<SelfRelated>->relations[0]) == metadata<SelfRelated>);

static_assert(metadata<RelationLookupCollision>.findRelation("children")->kind == orm::model::RelationKind::OneToMany);
static_assert(metadata<RelationLookupCollision>.findRelationField("parent")->kind == orm::model::RelationKind::ToOne);

static_assert(TestSchema::contains<TypedMapping>);
static_assert(not TestSchema::contains<int>);
static_assert(TestSchema::view.models.size() == 18);
static_assert(TestSchema::view.find(orm::model::typeId<CompositeId>()) == metadata<CompositeId>);
static_assert(TestSchema::view.findTable("typed_mappings") == metadata<TypedMapping>);
static_assert(TestSchema::view.findTable("missing") == nullptr);

constexpr auto typedDescriptor = orm::model::modelDescriptor<TestSchema, TypedMapping>();
static_assert(typedDescriptor.columns[1].name == "display_name");
} // namespace

TEST(StaticMetadataTest, exposesImmutableModelAndSchemaViews)
{
    const auto model = orm::model::modelView<TestSchema, TypedMapping>();
    const auto& schema = orm::model::schemaView<TestSchema>();

    ASSERT_NE(model.findColumn("displayName"), nullptr);
    EXPECT_EQ(model.findColumn("displayName"), model.findColumn("display_name"));
    EXPECT_EQ(schema.find(model->type), model);
}

TEST(StaticMetadataTest, relationCollectionsHaveValueSemantics)
{
    orm::OneToMany<DefaultId> children;
    orm::ManyToMany<DefaultId> related;

    EXPECT_FALSE(children.isLoaded());
    EXPECT_TRUE(children.empty());
    EXPECT_EQ(children.begin(), children.end());
    EXPECT_TRUE(related.values().empty());

    children.values().push_back({1, "first"});
    children.values().push_back({2, "second"});
    auto copy = children;

    children[0].id = 3;
    children.values().push_back({4, "third"});

    ASSERT_EQ(children.size(), 3);
    EXPECT_EQ(children[0].id, 3);
    ASSERT_EQ(copy.size(), 2);
    EXPECT_EQ(copy[0].id, 1);
}

TEST(StaticMetadataTest, constructedCollectionIsLoaded)
{
    const orm::ManyToMany<DefaultId> related{{{1, "first"}, {2, "second"}}};

    EXPECT_TRUE(related.isLoaded());
    ASSERT_EQ(related.size(), 2);
    EXPECT_EQ(related[0].id, 1);
    EXPECT_EQ(related[1].value, "second");
}
