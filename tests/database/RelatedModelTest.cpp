#include "DatabaseTest.hpp"

class RelatedModelTest : public DatabaseTest
{
};

TEST_P(RelatedModelTest, shouldCreateTableWithRelatedModel)
{
    createTable<models::ModelWithId>();

    EXPECT_NO_THROW(createTable<models::ModelRelatedToOtherModel>());
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, RelatedModelTest, connectionStrings);