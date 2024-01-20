#include "orm-cxx/model/IdInfo.hpp"

#include <gtest/gtest.h>

#include "tests/ModelsDefinitions.hpp"

TEST(IdInfoTest, modelWithId)
{
    std::unordered_set<std::string> ids{"id"};

    EXPECT_EQ(orm::model::getPrimaryIdColumnsNames<models::ModelWithId>(), ids);

    EXPECT_TRUE(orm::model::checkIfIsModelWithId<models::ModelWithId>());
}

TEST(IdInfoTest, modelWithOverwrittenId)
{
    std::unordered_set<std::string> ids{"field1", "field2"};

    EXPECT_EQ(orm::model::getPrimaryIdColumnsNames<models::ModelWithOverwrittenId>(), ids);

    EXPECT_TRUE(orm::model::checkIfIsModelWithId<models::ModelWithOverwrittenId>());
}

TEST(IdInfoTest, modelWithoutId)
{
    EXPECT_EQ(orm::model::getPrimaryIdColumnsNames<models::ModelWithOneField>(), std::unordered_set<std::string>{});

    EXPECT_FALSE(orm::model::checkIfIsModelWithId<models::ModelWithOneField>());
}
