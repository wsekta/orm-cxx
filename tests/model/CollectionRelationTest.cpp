#include <algorithm>
#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "orm-cxx/model.hpp"
#include "orm-cxx/relations.hpp"
#include "tests/CollectionModelsDefinitions.hpp"
#include "tests/ModelsDefinitions.hpp"

namespace collection_test_models
{
struct MissingDescriptor
{
    int id;
    orm::OneToMany<models::ModelWithId> children;
};

struct InvalidMappedBy
{
    int id;
    orm::OneToMany<models::ModelWithId> children;

    inline static const auto relations = orm::relations(orm::oneToMany("children").mappedBy("missing"));
};

struct MissingJunction
{
    int id;
    orm::ManyToMany<models::ModelWithId> related;

    inline static const auto relations = orm::relations(orm::manyToMany("related"));
};

struct InvalidJunctionColumns
{
    int id;
    orm::ManyToMany<models::ModelWithId> related;

    inline static const auto relations = orm::relations(orm::manyToMany("related")
                                                            .through("invalid_junction")
                                                            .ownerColumns({"owner_id", "extra_owner_id"})
                                                            .targetColumns({"target_id"}));
};

struct ConflictingJunctions
{
    int id;
    orm::ManyToMany<models::ModelWithId> first;
    orm::ManyToMany<models::ModelWithId> second;

    inline static const auto relations = orm::relations(orm::manyToMany("first").through("conflicting_junction"),
                                                        orm::manyToMany("second").through("conflicting_junction"));
};

struct SelfRelated
{
    int id;
    orm::ManyToMany<SelfRelated> peers;

    inline static const auto relations = orm::relations(orm::manyToMany("peers").through("self_links"));
};

struct RelationToKeylessTarget
{
    int id;
    orm::ManyToMany<models::ModelWithOneField> related;

    inline static const auto relations = orm::relations(orm::manyToMany("related").through("keyless_target_links"));
};

struct KeylessRelationOwner
{
    std::string name;
    orm::ManyToMany<models::ModelWithId> related;

    inline static const auto relations = orm::relations(orm::manyToMany("related").through("keyless_owner_links"));
};

struct DefaultJunctionOwner
{
    inline static constexpr std::string_view table_name = "default_junction_owners";

    int id;
    orm::ManyToMany<models::ModelWithId> related;

    inline static const auto relations = orm::relations(orm::manyToMany("related").through("default_junction"));
};

struct FirstGlobalJunctionOwner
{
    int id;
    orm::ManyToMany<models::ModelWithId> related;

    inline static const auto relations = orm::relations(orm::manyToMany("related")
                                                            .through("globally_conflicting_junction")
                                                            .ownerColumns({"first_owner_id"})
                                                            .targetColumns({"target_id"}));
};

struct SecondGlobalJunctionOwner
{
    int id;
    orm::ManyToMany<models::ModelWithId> related;

    inline static const auto relations = orm::relations(orm::manyToMany("related")
                                                            .through("globally_conflicting_junction")
                                                            .ownerColumns({"second_owner_id"})
                                                            .targetColumns({"target_id"}));
};

struct OptionalCollection
{
    int id;
    std::optional<orm::OneToMany<models::ModelWithId>> children;

    inline static const auto relations = orm::relations(orm::oneToMany("children").mappedBy("field3"));
};

struct ExplicitlyKeylessModel
{
    inline static const std::vector<std::string> id_columns{};

    int id;
};
} // namespace collection_test_models

using collection_test_models::ConflictingJunctions;
using collection_test_models::DefaultJunctionOwner;
using collection_test_models::ExplicitlyKeylessModel;
using collection_test_models::FirstGlobalJunctionOwner;
using collection_test_models::InvalidJunctionColumns;
using collection_test_models::InvalidMappedBy;
using collection_test_models::KeylessRelationOwner;
using collection_test_models::MissingDescriptor;
using collection_test_models::MissingJunction;
using collection_test_models::OptionalCollection;
using collection_test_models::RelationToKeylessTarget;
using collection_test_models::SecondGlobalJunctionOwner;
using collection_test_models::SelfRelated;

TEST(CollectionRelationTest, defaultCollection_shouldBeEmptyAndNotLoaded)
{
    orm::OneToMany<models::ModelWithId> children;
    orm::ManyToMany<models::ModelWithId> related;

    EXPECT_FALSE(children.isLoaded());
    EXPECT_TRUE(children.empty());
    EXPECT_EQ(children.size(), 0);
    EXPECT_EQ(children.begin(), children.end());
    EXPECT_TRUE(children.values().empty());

    EXPECT_FALSE(related.isLoaded());
    EXPECT_TRUE(related.empty());
    EXPECT_EQ(related.size(), 0);
    EXPECT_EQ(related.begin(), related.end());
    EXPECT_TRUE(related.values().empty());
}

TEST(CollectionRelationTest, collectionAccessors_shouldExposeStoredValues)
{
    orm::OneToMany<models::ModelWithId> children;
    children.values().push_back({1, 10, "first"});
    children.values().push_back({2, 20, "second"});

    EXPECT_EQ(children.size(), 2);
    EXPECT_EQ(children[0].id, 1);
    EXPECT_EQ(children[1].field2, "second");

    const auto& constChildren = children;
    EXPECT_EQ(constChildren.values().size(), 2);
    EXPECT_EQ(constChildren[0].field1, 10);
    EXPECT_EQ(std::distance(constChildren.begin(), constChildren.end()), 2);
}

TEST(CollectionRelationTest, constructedCollection_shouldStoreValuesAndBeLoaded)
{
    orm::ManyToMany<models::ModelWithId> related{{{1, 10, "first"}, {2, 20, "second"}}};

    EXPECT_TRUE(related.isLoaded());
    ASSERT_EQ(related.size(), 2);
    EXPECT_EQ(related[0].id, 1);
    EXPECT_EQ(related[1].id, 2);
}

TEST(CollectionRelationTest, copiedCollection_shouldKeepValueSemantics)
{
    orm::OneToMany<models::ModelWithId> original{{{1, 10, "first"}}};
    auto& retainedReference = original[0];
    auto retainedIterator = original.begin();
    auto copy = original;

    retainedReference.id = 2;
    retainedIterator->field1 = 20;
    original.values().push_back({3, 30, "third"});

    ASSERT_EQ(original.size(), 2);
    EXPECT_EQ(original[0].id, 2);
    EXPECT_EQ(original[0].field1, 20);
    ASSERT_EQ(copy.size(), 1);
    EXPECT_EQ(copy[0].id, 1);
    EXPECT_EQ(copy[0].field1, 10);
}

TEST(CollectionRelationTest, collectionFields_shouldBeRelationsAndNotScalarColumns)
{
    const auto& authorInfo = orm::Model<collection_models::Author>::getModelInfo(true);
    const auto& bookInfo = orm::Model<collection_models::Book>::getModelInfo(true);
    const auto& userInfo = orm::Model<collection_models::User>::getModelInfo(true);
    const auto& roleInfo = orm::Model<collection_models::Role>::getModelInfo(true);

    EXPECT_EQ(authorInfo.columnsInfo.size(), 2);
    ASSERT_EQ(authorInfo.relationsInfo.size(), 1);
    EXPECT_EQ(authorInfo.relationsInfo[0].fieldName, "books");
    EXPECT_EQ(authorInfo.relationsInfo[0].kind, orm::model::RelationKind::OneToMany);
    EXPECT_EQ(authorInfo.relationsInfo[0].mappedBy, "author");
    EXPECT_EQ(authorInfo.relationsInfo[0].targetModel().tableName, collection_models::Book::table_name);

    ASSERT_NE(bookInfo.findRelation("author"), nullptr);
    EXPECT_EQ(bookInfo.findRelation("author")->kind, orm::model::RelationKind::ToOne);
    EXPECT_TRUE(bookInfo.findRelation("author")->nullable);

    EXPECT_EQ(userInfo.columnsInfo.size(), 2);
    ASSERT_EQ(userInfo.relationsInfo.size(), 1);
    ASSERT_TRUE(userInfo.relationsInfo[0].junction.has_value());
    EXPECT_EQ(userInfo.relationsInfo[0].kind, orm::model::RelationKind::ManyToMany);
    EXPECT_EQ(userInfo.relationsInfo[0].junction->tableName, "collection_user_roles");
    EXPECT_EQ(userInfo.relationsInfo[0].junction->ownerColumns, (std::vector<std::string>{"user_id"}));
    EXPECT_EQ(userInfo.relationsInfo[0].junction->targetColumns, (std::vector<std::string>{"role_id"}));
    EXPECT_TRUE(userInfo.relationsInfo[0].junction->owningSide);

    EXPECT_EQ(roleInfo.columnsInfo.size(), 2);
    ASSERT_EQ(roleInfo.relationsInfo.size(), 1);
    EXPECT_EQ(roleInfo.relationsInfo[0].mappedBy, "roles");
    EXPECT_EQ(roleInfo.relationsInfo[0].kind, orm::model::RelationKind::ManyToMany);
    ASSERT_TRUE(roleInfo.relationsInfo[0].junction.has_value());
    EXPECT_FALSE(roleInfo.relationsInfo[0].junction->owningSide);
}

TEST(CollectionRelationTest, manyToManyWithoutColumnOverrides_shouldDeriveJunctionColumnNames)
{
    const auto& modelInfo = orm::Model<DefaultJunctionOwner>::getModelInfo(true);

    ASSERT_EQ(modelInfo.relationsInfo.size(), 1);
    ASSERT_TRUE(modelInfo.relationsInfo[0].junction.has_value());
    EXPECT_EQ(modelInfo.relationsInfo[0].junction->ownerColumns,
              (std::vector<std::string>{"default_junction_owners_id"}));
    EXPECT_EQ(modelInfo.relationsInfo[0].junction->targetColumns, (std::vector<std::string>{"models_ModelWithId_id"}));
}

TEST(CollectionRelationTest, differentOwnersCannotClaimTheSameJunctionTable)
{
    EXPECT_NO_THROW((void)orm::Model<FirstGlobalJunctionOwner>::getModelInfo(true));
    EXPECT_THROW((void)orm::Model<SecondGlobalJunctionOwner>::getModelInfo(true), std::invalid_argument);
}

TEST(CollectionRelationTest, invalidCollectionMappings_shouldThrowDuringMetadataConstruction)
{
    EXPECT_THROW((void)orm::Model<MissingDescriptor>::getModelInfo(true), std::invalid_argument);
    EXPECT_THROW((void)orm::Model<InvalidMappedBy>::getModelInfo(true), std::invalid_argument);
    EXPECT_THROW((void)orm::Model<MissingJunction>::getModelInfo(true), std::invalid_argument);
    EXPECT_THROW((void)orm::Model<InvalidJunctionColumns>::getModelInfo(true), std::invalid_argument);
    EXPECT_THROW((void)orm::Model<ConflictingJunctions>::getModelInfo(true), std::invalid_argument);
    EXPECT_THROW((void)orm::Model<SelfRelated>::getModelInfo(true), std::invalid_argument);
    EXPECT_THROW((void)orm::Model<RelationToKeylessTarget>::getModelInfo(true), std::invalid_argument);
    EXPECT_THROW((void)orm::Model<KeylessRelationOwner>::getModelInfo(true), std::invalid_argument);
    EXPECT_THROW((void)orm::Model<OptionalCollection>::getModelInfo(true), std::invalid_argument);
}

TEST(CollectionRelationTest, explicitlyEmptyIdColumns_shouldKeepModelKeyless)
{
    const auto& modelInfo = orm::Model<ExplicitlyKeylessModel>::getModelInfo(true);

    EXPECT_FALSE(orm::model::checkIfIsModelWithId<ExplicitlyKeylessModel>());
    EXPECT_TRUE(modelInfo.idColumnsNames.empty());
    ASSERT_EQ(modelInfo.columnsInfo.size(), 1);
    EXPECT_FALSE(modelInfo.columnsInfo[0].isPrimaryKey);
}
